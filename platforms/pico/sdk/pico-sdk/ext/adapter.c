#include "pico-c.h"
// The private ABI beneath platform.pico.mcu, declared beside that module
// rather than here: this file implements it, and the module calls it.
#include "../mcu/mcu-c.h"
#include "../stdio/stdio-c.h"
#include "../usb/device/usb-device-c.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "pico/flash.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"
#include "pico/stdio_semihosting.h"
#include "pico/stdio_uart.h"
#if !MM_BOARD_USB_PORT_APPLICATION
#include "pico/stdio_usb.h"
#endif
#include "pico/time.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if !MM_BOARD_USB_PORT_APPLICATION || MM_BOARD_HAS_USB_HOST
#include "tusb.h"
#endif
#if !MM_BOARD_USB_PORT_APPLICATION
#include "device/usbd_pvt.h"
#endif
#if MM_BOARD_HAS_USB_HOST
#include "pio_usb.h"
#endif

// The vendor surface: ext.pico, for code that wants Pico SDK specifically.

static int mm_pico_stdio_attempted;
static int mm_pico_stdio_ready;

#ifndef MM_PICO_STDIO_USB_CONNECT_DELAY_MS
#define MM_PICO_STDIO_USB_CONNECT_DELAY_MS 500
#endif

#if !MM_BOARD_USB_PORT_APPLICATION
// When the device was last configured or resumed. Only TinyUSB's device
// callbacks write these, from the SDK's USB worker interrupt; the connection
// query only reads them. The stamp is 32-bit milliseconds so that every read
// and write is a single access the interrupt cannot split, and it is written
// before the flag, so a reader that sees the flag sees its stamp.
static volatile uint32_t mm_pico_usb_mounted_ms;
static volatile int mm_pico_usb_mounted;

static uint32_t mm_pico_usb_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void tud_mount_cb(void) {
    mm_pico_usb_mounted_ms = mm_pico_usb_now_ms();
    mm_pico_usb_mounted = 1;
}

void tud_umount_cb(void) {
    mm_pico_usb_mounted = 0;
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    mm_pico_usb_mounted = 0;
}

void tud_resume_cb(void) {
    mm_pico_usb_mounted_ms = mm_pico_usb_now_ms();
    mm_pico_usb_mounted = 1;
}

// Connected once the host asserts DTR, or, for a terminal that never does,
// once the delay has passed since the device was configured or resumed. The
// delay is measured with the system timer, which stops while a debugger
// halts either core; DTR does not depend on it.
static int mm_pico_usb_is_connected(void) {
    if (!stdio_usb_connected() || !mm_pico_usb_mounted) return 0;
    if (tud_cdc_connected()) return 1;
#if MM_PICO_STDIO_USB_CONNECT_DELAY_MS > 0
    return (uint32_t)(mm_pico_usb_now_ms() - mm_pico_usb_mounted_ms) >=
           (uint32_t)MM_PICO_STDIO_USB_CONNECT_DELAY_MS;
#else
    return 1;
#endif
}

// The CDC data interface's bulk IN endpoint, read once from the configuration
// descriptor pico_stdio_usb supplies; 0 if there is none.
static uint8_t mm_pico_usb_cdc_in_ep(void) {
    static int searched;
    static uint8_t found;
    if (searched) return found;
    searched = 1;
    uint8_t const* p = tud_descriptor_configuration_cb(0);
    if (p == NULL) return found;
    uint8_t const* const end = p + tu_u16(p[3], p[2]);
    int data = 0;
    for (; p + 2 <= end && p[0] >= 2; p += p[0]) {
        if (p[1] == TUSB_DESC_INTERFACE) {
            data = p[5] == TUSB_CLASS_CDC_DATA;
        } else if (data && p[1] == TUSB_DESC_ENDPOINT &&
                   (p[2] & TUSB_DIR_IN_MASK) &&
                   (p[3] & 3) == TUSB_XFER_BULK) {
            found = p[2];
            break;
        }
    }
    return found;
}

// pico_stdio_usb's flush only starts a transfer. While DTR is asserted, wait
// for the FIFO and IN endpoint to drain so an immediate _exit does not leave
// the last bytes on the board. With no terminal open, pending output may
// never drain: without DTR this remains a best-effort flush with no added
// wait. DTR is a policy gate, not proof that the host is reading. The wait is
// bounded by time and, because a debugger can stop the timer, by a count.
#define MM_PICO_STDIO_DRAIN_MS 500
#define MM_PICO_STDIO_DRAIN_SPINS 200000u

static int mm_pico_usb_drain(void) {
    const uint8_t ep = mm_pico_usb_cdc_in_ep();
    if (ep == 0) return MM_PICO_STDIO_OK;
    const absolute_time_t deadline =
        make_timeout_time_ms(MM_PICO_STDIO_DRAIN_MS);
    for (uint32_t spin = 0; spin < MM_PICO_STDIO_DRAIN_SPINS; ++spin) {
        // Take one snapshot without the local USB worker changing the FIFO or
        // completing/resetting the endpoint between these observations.
        const uint32_t saved = save_and_disable_interrupts();
        const bool connected = stdio_usb_connected();
        const bool dtr = tud_cdc_connected();
        const bool drained =
            tud_cdc_write_available() == CFG_TUD_CDC_TX_BUFSIZE &&
            !usbd_edpt_busy(0, ep);
        restore_interrupts(saved);
        if (drained) return MM_PICO_STDIO_OK;
        if (!connected || !dtr) return MM_PICO_STDIO_OK;
        if (time_reached(deadline)) break;
        stdio_usb.out_flush();
    }
    return MM_PICO_STDIO_TIMEOUT;
}
#endif

#if MM_BOARD_HAS_USB_HOST
// A board with a PIO USB host port. Pico-PIO-USB bit-bangs full-speed USB and
// needs the system clock at a multiple of 12 MHz. The clock is set before
// anything else runs, in a constructor ahead of every C++ static initializer,
// because the peripheral clock follows it and a UART or SPI configured at the
// default clock would be left at the wrong rate.
__attribute__((constructor(101))) static void mm_pico_usb_host_clock(void) {
    set_sys_clock_khz(120000, true);
}

static int mm_pico_usb_started;

// TinyUSB before pico_stdio_usb, which expects it initialised when the host is
// linked: the native port as the console device, the PIO port as the host.
static void mm_pico_usb_start(void) {
    if (mm_pico_usb_started) return;
    mm_pico_usb_started = 1;
    pio_usb_configuration_t configuration = PIO_USB_DEFAULT_CONFIG;
    configuration.pin_dp = MM_BOARD_USB_HOST_DP_PIN;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &configuration);
    tud_init(BOARD_TUD_RHPORT);
    tuh_init(BOARD_TUH_RHPORT);
}
#endif

static void mm_pico_initialize_stdio(void) {
    if (mm_pico_stdio_attempted) return;
    mm_pico_stdio_attempted = 1;

    // stdio_init_all reports whether any enabled driver initialized. With
    // semihosting present that cannot tell mm.stdio whether USB succeeded, and
    // following it with stdio_usb_init would initialize TinyUSB and its IRQ
    // machinery twice. Initialize each bridge-enabled driver once instead and
    // retain the USB result separately.
#if LIB_PICO_STDIO_UART
    stdio_uart_init();
#endif
#if LIB_PICO_STDIO_SEMIHOSTING
    stdio_semihosting_init();
#endif
#if MM_BOARD_HAS_USB_HOST
    mm_pico_usb_start();
#endif
#if MM_BOARD_USB_PORT_APPLICATION
#if LIB_PICO_STDIO_UART
    mm_pico_stdio_ready = 1;
#endif
#elif LIB_PICO_STDIO_USB
    mm_pico_stdio_ready = stdio_usb_init() ? 1 : 0;
#endif
}

void mm_pico_initialize(void) {
    mm_pico_initialize_stdio();
}

void mm_pico_write(const char* text) {
    puts(text);
}

void mm_pico_delay_ms(unsigned long milliseconds) {
    sleep_ms(milliseconds);
}

void mm_pico_gpio_write(unsigned int pin, int high) {
    gpio_init(pin);
    gpio_set_dir(pin, 1);
    gpio_put(pin, high);
}

// The platform surface beneath platform.pico.stdio. It addresses the USB
// driver directly: stdio_put_string multiplexes every enabled driver and
// always returns its requested length, so it cannot describe what USB
// accepted. The SDK's USB output callback also returns void; this adapter can
// therefore report zero when USB is disconnected and the full request when a
// connected call returns, but cannot observe a mid-call timeout after only
// part of the transfer reached the host.

int mm_pico_stdio_initialize(void) {
    mm_pico_initialize_stdio();
    return mm_pico_stdio_ready ? MM_PICO_STDIO_OK
                               : MM_PICO_STDIO_TRANSPORT_ERROR;
}

int mm_pico_stdio_write(const unsigned char* data, size_t size, size_t* written) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (written == NULL || size > INT_MAX || (size != 0 && data == NULL))
        return MM_PICO_STDIO_BAD_ARGUMENT;

    *written = 0;
#if MM_BOARD_USB_PORT_APPLICATION
    if (size == 0) return MM_PICO_STDIO_OK;
#if LIB_PICO_STDIO_UART
    if (stdio_uart.out_chars == NULL) return MM_PICO_STDIO_UNSUPPORTED;
    stdio_uart.out_chars((const char*)data, (int)size);
    *written = size;
    return MM_PICO_STDIO_OK;
#else
    return MM_PICO_STDIO_UNSUPPORTED;
#endif
#else
    if (size == 0 || !mm_pico_usb_is_connected()) return MM_PICO_STDIO_OK;
    if (stdio_usb.out_chars == NULL) return MM_PICO_STDIO_UNSUPPORTED;

    stdio_usb.out_chars((const char*)data, (int)size);
    *written = size;
    return MM_PICO_STDIO_OK;
#endif
}

int mm_pico_stdio_read(unsigned char* data, size_t size, size_t* count) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (count == NULL || size > INT_MAX || (size != 0 && data == NULL))
        return MM_PICO_STDIO_BAD_ARGUMENT;

    *count = 0;
#if MM_BOARD_USB_PORT_APPLICATION
    if (size == 0) return MM_PICO_STDIO_OK;
#if LIB_PICO_STDIO_UART
    if (stdio_uart.in_chars == NULL) return MM_PICO_STDIO_UNSUPPORTED;
    const int result = stdio_uart.in_chars((char*)data, (int)size);
    if (result > 0) {
        *count = (size_t)result;
        return MM_PICO_STDIO_OK;
    }
    if (result == PICO_ERROR_NO_DATA) return MM_PICO_STDIO_OK;
    return MM_PICO_STDIO_TRANSPORT_ERROR;
#else
    return MM_PICO_STDIO_UNSUPPORTED;
#endif
#else
    if (size == 0 || !mm_pico_usb_is_connected()) return MM_PICO_STDIO_OK;
    if (stdio_usb.in_chars == NULL) return MM_PICO_STDIO_UNSUPPORTED;

    const int result = stdio_usb.in_chars((char*)data, (int)size);
    if (result > 0) {
        *count = (size_t)result;
        return MM_PICO_STDIO_OK;
    }
    if (result == PICO_ERROR_NO_DATA) return MM_PICO_STDIO_OK;
    return MM_PICO_STDIO_TRANSPORT_ERROR;
#endif
}

int mm_pico_stdio_flush(void) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
#if MM_BOARD_USB_PORT_APPLICATION
#if LIB_PICO_STDIO_UART
    if (stdio_uart.out_flush == NULL) return MM_PICO_STDIO_UNSUPPORTED;
    stdio_uart.out_flush();
    return MM_PICO_STDIO_OK;
#else
    return MM_PICO_STDIO_UNSUPPORTED;
#endif
#else
    if (stdio_usb.out_flush == NULL) return MM_PICO_STDIO_UNSUPPORTED;
    stdio_usb.out_flush();
    return mm_pico_usb_drain();
#endif
}

int mm_pico_stdio_connected(int* connected) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (connected == NULL) return MM_PICO_STDIO_BAD_ARGUMENT;
#if MM_BOARD_USB_PORT_APPLICATION
    *connected = 1;
    return MM_PICO_STDIO_OK;
#else
    *connected = mm_pico_usb_is_connected();
    return MM_PICO_STDIO_OK;
#endif
}

// The platform surface: the private ABI beneath platform.pico.mcu. Both live in
// one adapter because this is the only translation unit permitted to include SDK
// headers, and a second one would be a second place for that permission to be
// reviewed. These names are this bridge's own; the portable contract is mm.mcu,
// and it is pure C++.

static int mm_pico_pin_valid(unsigned int pin) {
    return pin < (unsigned int)NUM_BANK0_GPIOS;
}

static volatile int mm_pico_gpio_watched[NUM_BANK0_GPIOS];
static volatile int mm_pico_gpio_pending[NUM_BANK0_GPIOS];
static unsigned int mm_pico_gpio_mask[NUM_BANK0_GPIOS];
static int mm_pico_gpio_callback_ready;

// Who holds a pad. A plain GPIO configuration yields to an analog claim; a
// watch and an analog claim yield only to their own release. The watched mark
// above is what the interrupt reads; this record is what the facilities
// consult, and the two agree because every transition sets both.
enum {
    MM_PICO_OWNER_NONE = 0,
    MM_PICO_OWNER_GPIO = 1,
    MM_PICO_OWNER_WATCHED = 2,
    MM_PICO_OWNER_ADC = 3,
    MM_PICO_OWNER_PWM = 4,
    MM_PICO_OWNER_I2S = 5,
    MM_PICO_OWNER_PULSE = 6,
    MM_PICO_OWNER_SDIO = 7,
    MM_PICO_OWNER_CAMERA = 8
};
static unsigned char mm_pico_pin_owner[NUM_BANK0_GPIOS];
static int mm_pico_camera_claimed;

// A pad a peripheral holds: an analog claim, an I2S link's, a pulse
// output's, or an SD bus's.
static int mm_pico_analog_holds(unsigned int pin) {
    return mm_pico_pin_owner[pin] == MM_PICO_OWNER_ADC ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_PWM ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_I2S ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_PULSE ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_SDIO ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_CAMERA;
}

static void mm_pico_gpio_callback(unsigned int pin, uint32_t events) {
    if (mm_pico_pin_valid(pin) && mm_pico_gpio_watched[pin] &&
        (events & mm_pico_gpio_mask[pin])) {
        mm_pico_gpio_pending[pin] = 1;
        __sev();
    }
}

static spi_inst_t* mm_pico_spi(unsigned int instance) {
    if (instance == 0) return spi0;
    if (instance == 1) return spi1;
    return NULL;
}

static int mm_pico_spi_ready[2];

// On RP2040 and RP2350 the SPI signal pattern repeats every sixteen GPIOs:
// RX, CSn, SCK, TX for SPI0 in each lower group of eight and for SPI1 in each
// upper group. Chip select is deliberately GPIO policy above this transport.
static int mm_pico_spi_pin_matches(unsigned int instance, unsigned int pin,
                                   unsigned int signal) {
    return ((pin / 8u) % 2u) == instance && pin % 4u == signal;
}

int mm_pico_mcu_gpio_configure(unsigned int pin, int direction, int pull) {
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_gpio_watched[pin] || mm_pico_analog_holds(pin)) return MM_PICO_MCU_BUSY;
    if (direction != MM_PICO_MCU_DIRECTION_IN && direction != MM_PICO_MCU_DIRECTION_OUT)
        return MM_PICO_MCU_BAD_ARGUMENT;
    if (pull != MM_PICO_MCU_PULL_NONE && pull != MM_PICO_MCU_PULL_UP &&
        pull != MM_PICO_MCU_PULL_DOWN) return MM_PICO_MCU_BAD_ARGUMENT;

    gpio_init(pin);
    gpio_set_dir(pin, direction == MM_PICO_MCU_DIRECTION_OUT);

    switch (pull) {
        case MM_PICO_MCU_PULL_NONE: gpio_disable_pulls(pin); break;
        case MM_PICO_MCU_PULL_UP: gpio_pull_up(pin); break;
        case MM_PICO_MCU_PULL_DOWN: gpio_pull_down(pin); break;
        default: return MM_PICO_MCU_BAD_ARGUMENT;
    }
    mm_pico_pin_owner[pin] = MM_PICO_OWNER_GPIO;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_write(unsigned int pin, int high) {
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_analog_holds(pin)) return MM_PICO_MCU_BUSY;
    gpio_put(pin, high != 0);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_read(unsigned int pin, int* high) {
    if (!mm_pico_pin_valid(pin) || high == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_analog_holds(pin)) return MM_PICO_MCU_BUSY;
    *high = gpio_get(pin) ? 1 : 0;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_watch(unsigned int pin, int pull, int edge) {
    if (get_core_num() != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_UNSUPPORTED;
    if ((pull != MM_PICO_MCU_PULL_NONE && pull != MM_PICO_MCU_PULL_UP &&
         pull != MM_PICO_MCU_PULL_DOWN) ||
        (edge != MM_PICO_MCU_EDGE_RISING && edge != MM_PICO_MCU_EDGE_FALLING &&
         edge != MM_PICO_MCU_EDGE_BOTH)) return MM_PICO_MCU_BAD_ARGUMENT;
    uint32_t saved = save_and_disable_interrupts();
    if (mm_pico_gpio_watched[pin] || mm_pico_analog_holds(pin)) {
        restore_interrupts(saved);
        return MM_PICO_MCU_BUSY;
    }
    const int configured = mm_pico_mcu_gpio_configure(
        pin, MM_PICO_MCU_DIRECTION_IN, pull);
    if (configured != MM_PICO_MCU_OK) {
        restore_interrupts(saved);
        return configured;
    }
    const unsigned int mask =
        (edge == MM_PICO_MCU_EDGE_RISING ? GPIO_IRQ_EDGE_RISE : 0u) |
        (edge == MM_PICO_MCU_EDGE_FALLING ? GPIO_IRQ_EDGE_FALL : 0u) |
        (edge == MM_PICO_MCU_EDGE_BOTH ?
             GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL : 0u);
    if (!mm_pico_gpio_callback_ready) {
        gpio_set_irq_callback(mm_pico_gpio_callback);
        irq_set_enabled(IO_IRQ_BANK0, true);
        mm_pico_gpio_callback_ready = 1;
    }
    gpio_acknowledge_irq(pin, mask);
    mm_pico_gpio_mask[pin] = mask;
    mm_pico_gpio_pending[pin] = 0;
    mm_pico_gpio_watched[pin] = 1;
    mm_pico_pin_owner[pin] = MM_PICO_OWNER_WATCHED;
    gpio_set_irq_enabled(pin, mask, true);
    restore_interrupts(saved);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_take(unsigned int pin, int* pending) {
    if (get_core_num() != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_UNSUPPORTED;
    if (pending == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    const uint32_t saved = save_and_disable_interrupts();
    if (!mm_pico_gpio_watched[pin]) {
        restore_interrupts(saved);
        return MM_PICO_MCU_BAD_ARGUMENT;
    }
    const int value = mm_pico_gpio_pending[pin];
    mm_pico_gpio_pending[pin] = 0;
    restore_interrupts(saved);
    *pending = value;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_unwatch(unsigned int pin) {
    if (get_core_num() != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_UNSUPPORTED;
    const uint32_t saved = save_and_disable_interrupts();
    if (!mm_pico_gpio_watched[pin]) {
        restore_interrupts(saved);
        return MM_PICO_MCU_BAD_ARGUMENT;
    }
    mm_pico_gpio_watched[pin] = 0;
    mm_pico_pin_owner[pin] = MM_PICO_OWNER_NONE;
    gpio_set_irq_enabled(pin, mm_pico_gpio_mask[pin], false);
    mm_pico_gpio_pending[pin] = 0;
    gpio_deinit(pin);
    // Leave the generic callback and bank IRQ installed. They are shared by
    // future watches; the watched mark suppresses any stale NVIC delivery.
    // The saved mask is overwritten by the next watch on this pin.
    restore_interrupts(saved);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_wait(unsigned int pin, unsigned long timeout_ms, int* pending) {
    if (get_core_num() != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_UNSUPPORTED;
    if (pending == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    for (;;) {
        int value = 0;
        const int status = mm_pico_mcu_gpio_take(pin, &value);
        if (status != MM_PICO_MCU_OK) return status;
        if (value || time_reached(deadline)) {
            *pending = value;
            return MM_PICO_MCU_OK;
        }
        best_effort_wfe_or_timeout(deadline);
    }
}

int mm_pico_mcu_spi_configure(unsigned int instance, unsigned int clock_pin,
                              unsigned int transmit_pin, unsigned int receive_pin,
                              int has_receive, unsigned long baud, int mode,
                              int least_significant_first) {
    spi_inst_t* spi = mm_pico_spi(instance);
    if (spi == NULL || baud == 0 || !mm_pico_pin_valid(clock_pin) ||
        !mm_pico_pin_valid(transmit_pin) ||
        (has_receive && !mm_pico_pin_valid(receive_pin)) ||
        clock_pin == transmit_pin ||
        (has_receive && (receive_pin == clock_pin || receive_pin == transmit_pin)) ||
        !mm_pico_spi_pin_matches(instance, clock_pin, 2u) ||
        !mm_pico_spi_pin_matches(instance, transmit_pin, 3u) ||
        (has_receive && !mm_pico_spi_pin_matches(instance, receive_pin, 0u)) ||
        mode < 0 || mode > 3)
        return MM_PICO_MCU_BAD_ARGUMENT;

    if (mm_pico_pin_owner[clock_pin] == MM_PICO_OWNER_CAMERA ||
        mm_pico_pin_owner[transmit_pin] == MM_PICO_OWNER_CAMERA ||
        (has_receive && mm_pico_pin_owner[receive_pin] == MM_PICO_OWNER_CAMERA))
        return MM_PICO_MCU_BUSY;
    spi_init(spi, (uint)baud);
    gpio_set_function(clock_pin, GPIO_FUNC_SPI);
    gpio_set_function(transmit_pin, GPIO_FUNC_SPI);
    if (has_receive) gpio_set_function(receive_pin, GPIO_FUNC_SPI);

    const spi_cpol_t polarity = mode >= 2 ? SPI_CPOL_1 : SPI_CPOL_0;
    const spi_cpha_t phase = (mode & 1) != 0 ? SPI_CPHA_1 : SPI_CPHA_0;
    spi_set_format(spi, 8, polarity, phase,
                   least_significant_first ? SPI_LSB_FIRST : SPI_MSB_FIRST);
    mm_pico_spi_ready[instance] = 1;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_spi_write(unsigned int instance, const unsigned char* data, size_t size) {
    spi_inst_t* spi = mm_pico_spi(instance);
    if (spi == NULL || !mm_pico_spi_ready[instance] || size > INT_MAX ||
        (size != 0 && data == NULL))
        return MM_PICO_MCU_BAD_ARGUMENT;
    if (size == 0) return MM_PICO_MCU_OK;
    return spi_write_blocking(spi, data, size) == (int)size ? MM_PICO_MCU_OK
                                                            : MM_PICO_MCU_BUSY;
}

int mm_pico_mcu_spi_transfer(unsigned int instance, const unsigned char* transmit,
                             unsigned char* receive, size_t size) {
    spi_inst_t* spi = mm_pico_spi(instance);
    if (spi == NULL || !mm_pico_spi_ready[instance] || size > INT_MAX ||
        (size != 0 && (transmit == NULL || receive == NULL)))
        return MM_PICO_MCU_BAD_ARGUMENT;
    if (size == 0) return MM_PICO_MCU_OK;
    return spi_write_read_blocking(spi, transmit, receive, size) == (int)size
               ? MM_PICO_MCU_OK
               : MM_PICO_MCU_BUSY;
}

static i2c_inst_t* mm_pico_i2c(unsigned int instance) {
    if (instance == 0) return i2c0;
    if (instance == 1) return i2c1;
    return NULL;
}

static int mm_pico_i2c_ready[2];

// On RP2040 and RP2350 the I2C signal pattern repeats every four GPIOs: an even
// pin is SDA and the odd pin above it is SCL, and the instance alternates with
// each pair. Checking it here keeps a wrong pin a BadArgument rather than a
// silent bus that never acknowledges.
static int mm_pico_i2c_pin_matches(unsigned int instance, unsigned int pin,
                                   unsigned int signal) {
    return ((pin / 2u) % 2u) == instance && pin % 2u == signal;
}

// Seven-bit addresses only, matching the portable interface. The reserved
// ranges below 0x08 and above 0x77 are refused rather than sent.
static int mm_pico_i2c_address_valid(unsigned int address) {
    return address >= 0x08u && address <= 0x77u;
}

int mm_pico_mcu_i2c_configure(unsigned int instance, unsigned int data_pin,
                              unsigned int clock_pin, unsigned long baud) {
    i2c_inst_t* i2c = mm_pico_i2c(instance);
    if (i2c == NULL || baud == 0 || !mm_pico_pin_valid(data_pin) ||
        !mm_pico_pin_valid(clock_pin) || data_pin == clock_pin ||
        !mm_pico_i2c_pin_matches(instance, data_pin, 0u) ||
        !mm_pico_i2c_pin_matches(instance, clock_pin, 1u))
        return MM_PICO_MCU_BAD_ARGUMENT;

    if ((instance == 0 && mm_pico_camera_claimed) ||
        mm_pico_pin_owner[data_pin] == MM_PICO_OWNER_CAMERA ||
        mm_pico_pin_owner[clock_pin] == MM_PICO_OWNER_CAMERA) return MM_PICO_MCU_BUSY;
    i2c_init(i2c, (uint)baud);
    gpio_set_function(data_pin, GPIO_FUNC_I2C);
    gpio_set_function(clock_pin, GPIO_FUNC_I2C);
    gpio_pull_up(data_pin);
    gpio_pull_up(clock_pin);
    mm_pico_i2c_ready[instance] = 1;
    return MM_PICO_MCU_OK;
}

// A zero-length write is the bus's device-presence probe, and the SDK rejects
// it, so it is refused here rather than turned into a transfer that means
// something else.
int mm_pico_mcu_i2c_write(unsigned int instance, unsigned int address,
                          const unsigned char* data, size_t size) {
    i2c_inst_t* i2c = mm_pico_i2c(instance);
    if (i2c == NULL || !mm_pico_i2c_ready[instance] ||
        !mm_pico_i2c_address_valid(address) || size == 0 || size > INT_MAX ||
        data == NULL)
        return MM_PICO_MCU_BAD_ARGUMENT;
    const int written = i2c_write_blocking(i2c, (uint8_t)address, data, size, false);
    if (written == (int)size) return MM_PICO_MCU_OK;
    return written < 0 ? MM_PICO_MCU_UNSUPPORTED : MM_PICO_MCU_BUSY;
}

int mm_pico_mcu_i2c_read(unsigned int instance, unsigned int address,
                         unsigned char* data, size_t size) {
    i2c_inst_t* i2c = mm_pico_i2c(instance);
    if (i2c == NULL || !mm_pico_i2c_ready[instance] ||
        !mm_pico_i2c_address_valid(address) || size == 0 || size > INT_MAX ||
        data == NULL)
        return MM_PICO_MCU_BAD_ARGUMENT;
    const int read = i2c_read_blocking(i2c, (uint8_t)address, data, size, false);
    if (read == (int)size) return MM_PICO_MCU_OK;
    return read < 0 ? MM_PICO_MCU_UNSUPPORTED : MM_PICO_MCU_BUSY;
}

// The nostop argument on the write is the whole point: it holds the bus so the
// read below is a repeated start rather than a second transaction.
int mm_pico_mcu_i2c_write_read(unsigned int instance, unsigned int address,
                               const unsigned char* command, size_t command_size,
                               unsigned char* data, size_t size) {
    i2c_inst_t* i2c = mm_pico_i2c(instance);
    if (i2c == NULL || !mm_pico_i2c_ready[instance] ||
        !mm_pico_i2c_address_valid(address) || command_size == 0 || size == 0 ||
        command_size > INT_MAX || size > INT_MAX || command == NULL || data == NULL)
        return MM_PICO_MCU_BAD_ARGUMENT;

    const int written =
        i2c_write_blocking(i2c, (uint8_t)address, command, command_size, true);
    if (written != (int)command_size)
        return written < 0 ? MM_PICO_MCU_UNSUPPORTED : MM_PICO_MCU_BUSY;
    const int read = i2c_read_blocking(i2c, (uint8_t)address, data, size, false);
    if (read == (int)size) return MM_PICO_MCU_OK;
    return read < 0 ? MM_PICO_MCU_UNSUPPORTED : MM_PICO_MCU_BUSY;
}

// The byte calls below address a hardware UART by its number, 0 or 1, and
// each must be configured first. mm_pico_uart_ready records which are.
static uart_inst_t* mm_pico_uart(unsigned int instance) {
    if (instance == 0) return uart0;
    if (instance == 1) return uart1;
    return NULL;
}

static int mm_pico_uart_ready[2];

// On RP2040 and RP2350 a UART's TX is where pin % 4 is 0 and its RX where it
// is 1, and the instance alternates in runs of eight offset by four: GP0-3
// UART0, GP4-11 UART1, GP12-19 UART0, GP20-27 UART1, GP28-29 UART0, which
// is ((pin + 4) / 8) % 2.
static int mm_pico_uart_pin_matches(unsigned int instance, unsigned int pin,
                                    unsigned int signal) {
    return ((pin + 4u) / 8u) % 2u == instance && pin % 4u == signal;
}

// Portable text instance zero means the selected SDK board's default hardware
// UART. Every Raspberry Pi Pico board definition supplies its UART number and
// TX/RX pins. A second portable instance would need its own board description
// and is Unsupported rather than silently aliasing the default. A default UART
// already configured through mm_pico_mcu_uart_configure keeps its rate.
int mm_pico_mcu_uart_write(unsigned int instance, const char* text) {
    if (text == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;

    if (!mm_pico_uart_ready[PICO_DEFAULT_UART]) {
        uart_init(uart_default, PICO_DEFAULT_UART_BAUD_RATE);
        gpio_set_function(PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(PICO_DEFAULT_UART_RX_PIN, GPIO_FUNC_UART);
        mm_pico_uart_ready[PICO_DEFAULT_UART] = 1;
    }
    uart_puts(uart_default, text);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_uart_configure(unsigned int instance, unsigned int transmit_pin,
                               unsigned int receive_pin, unsigned long baud) {
    uart_inst_t* uart = mm_pico_uart(instance);
    if (uart == NULL || baud == 0 || !mm_pico_pin_valid(transmit_pin) ||
        !mm_pico_pin_valid(receive_pin) ||
        !mm_pico_uart_pin_matches(instance, transmit_pin, 0u) ||
        !mm_pico_uart_pin_matches(instance, receive_pin, 1u))
        return MM_PICO_MCU_BAD_ARGUMENT;

    if (mm_pico_pin_owner[transmit_pin] == MM_PICO_OWNER_CAMERA ||
        mm_pico_pin_owner[receive_pin] == MM_PICO_OWNER_CAMERA) return MM_PICO_MCU_BUSY;
    uart_init(uart, (uint)baud);
    gpio_set_function(transmit_pin, GPIO_FUNC_UART);
    gpio_set_function(receive_pin, GPIO_FUNC_UART);
    mm_pico_uart_ready[instance] = 1;
    return MM_PICO_MCU_OK;
}

// Neither call waits: the FIFOs take or give what they have.
int mm_pico_mcu_uart_send(unsigned int instance, const unsigned char* data, size_t size,
                          size_t* accepted) {
    uart_inst_t* uart = mm_pico_uart(instance);
    if (uart == NULL || !mm_pico_uart_ready[instance] || accepted == NULL ||
        (data == NULL && size != 0))
        return MM_PICO_MCU_BAD_ARGUMENT;
    size_t sent = 0;
    while (sent < size && uart_is_writable(uart)) uart_putc_raw(uart, (char)data[sent++]);
    *accepted = sent;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_uart_receive(unsigned int instance, unsigned char* data, size_t size,
                             size_t* count) {
    uart_inst_t* uart = mm_pico_uart(instance);
    if (uart == NULL || !mm_pico_uart_ready[instance] || count == NULL ||
        (data == NULL && size != 0))
        return MM_PICO_MCU_BAD_ARGUMENT;
    size_t taken = 0;
    while (taken < size && uart_is_readable(uart)) data[taken++] = (unsigned char)uart_getc(uart);
    *count = taken;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_uart_release(unsigned int instance) {
    uart_inst_t* uart = mm_pico_uart(instance);
    if (uart == NULL || !mm_pico_uart_ready[instance]) return MM_PICO_MCU_BAD_ARGUMENT;
    uart_deinit(uart);
    mm_pico_uart_ready[instance] = 0;
    return MM_PICO_MCU_OK;
}

// The board's default UART, from its SDK board header, and whether it has a
// second one the bridge's board table vouches for.
int mm_pico_mcu_default_uart(unsigned int* instance, unsigned int* transmit_pin,
                             unsigned int* receive_pin) {
#if defined(PICO_DEFAULT_UART) && defined(PICO_DEFAULT_UART_TX_PIN) && \
    defined(PICO_DEFAULT_UART_RX_PIN)
    *instance = (unsigned int)PICO_DEFAULT_UART;
    *transmit_pin = (unsigned int)PICO_DEFAULT_UART_TX_PIN;
    *receive_pin = (unsigned int)PICO_DEFAULT_UART_RX_PIN;
    return 1;
#else
    (void)instance;
    (void)transmit_pin;
    (void)receive_pin;
    return 0;
#endif
}

int mm_pico_mcu_has_second_uart(void) {
#if MM_BOARD_HAS_SECOND_UART
    return 1;
#else
    return 0;
#endif
}

int mm_pico_mcu_delay_ms(unsigned long milliseconds) {
    sleep_ms(milliseconds);
    return MM_PICO_MCU_OK;
}

// The converter. adc_init runs once, on the first claim; each read selects
// its input immediately before converting, since the selection is the
// converter's one piece of shared state.
#ifndef MM_ADC_REFERENCE_MV
#define MM_ADC_REFERENCE_MV 0
#endif

static int mm_pico_adc_ready;
static unsigned char mm_pico_adc_claimed[NUM_ADC_CHANNELS];

static int mm_pico_adc_channel_valid(unsigned int channel) {
    return channel < (unsigned int)NUM_ADC_CHANNELS;
}

static int mm_pico_adc_channel_has_pin(unsigned int channel) {
    return channel != (unsigned int)ADC_TEMPERATURE_CHANNEL_NUM;
}

unsigned int mm_pico_mcu_adc_channel_count(void) {
    return (unsigned int)NUM_ADC_CHANNELS;
}

unsigned int mm_pico_mcu_adc_base_pin(void) {
    return (unsigned int)ADC_BASE_PIN;
}

unsigned int mm_pico_mcu_adc_temperature_channel(void) {
    return (unsigned int)ADC_TEMPERATURE_CHANNEL_NUM;
}

unsigned int mm_pico_mcu_adc_reference_mv(void) {
    return (unsigned int)MM_ADC_REFERENCE_MV;
}

int mm_pico_mcu_adc_configure(unsigned int channel) {
    if (!mm_pico_adc_channel_valid(channel)) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_adc_claimed[channel]) return MM_PICO_MCU_OK;
    if (mm_pico_adc_channel_has_pin(channel)) {
        const unsigned int pin = (unsigned int)ADC_BASE_PIN + channel;
        if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_UNSUPPORTED;
        if (mm_pico_pin_owner[pin] == MM_PICO_OWNER_WATCHED ||
            mm_pico_pin_owner[pin] == MM_PICO_OWNER_PWM ||
            mm_pico_pin_owner[pin] == MM_PICO_OWNER_I2S ||
            mm_pico_pin_owner[pin] == MM_PICO_OWNER_PULSE ||
            mm_pico_pin_owner[pin] == MM_PICO_OWNER_CAMERA)
            return MM_PICO_MCU_BUSY;
        if (!mm_pico_adc_ready) {
            adc_init();
            mm_pico_adc_ready = 1;
        }
        // The takeover: whatever digital mode the pad had is gone with the
        // input buffer and the pulls.
        adc_gpio_init(pin);
        mm_pico_pin_owner[pin] = MM_PICO_OWNER_ADC;
    } else {
        if (!mm_pico_adc_ready) {
            adc_init();
            mm_pico_adc_ready = 1;
        }
        adc_set_temp_sensor_enabled(true);
    }
    mm_pico_adc_claimed[channel] = 1;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_adc_read(unsigned int channel, unsigned int* count) {
    if (!mm_pico_adc_channel_valid(channel) || count == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    if (!mm_pico_adc_claimed[channel]) return MM_PICO_MCU_BAD_ARGUMENT;
    adc_select_input(channel);
    *count = (unsigned int)adc_read();
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_adc_release(unsigned int channel) {
    if (!mm_pico_adc_channel_valid(channel)) return MM_PICO_MCU_BAD_ARGUMENT;
    if (!mm_pico_adc_claimed[channel]) return MM_PICO_MCU_OK;
    if (mm_pico_adc_channel_has_pin(channel)) {
        const unsigned int pin = (unsigned int)ADC_BASE_PIN + channel;
        gpio_deinit(pin);
        mm_pico_pin_owner[pin] = MM_PICO_OWNER_NONE;
    } else {
        adc_set_temp_sensor_enabled(false);
    }
    mm_pico_adc_claimed[channel] = 0;
    return MM_PICO_MCU_OK;
}

// The modulator. The C++ side keeps the periods and decides who may join a
// slice; the adapter keeps the pad and comparator ownership, programs the
// slice the first time an output claims it, and stops it when the last one
// leaves. A level of top + 1 is a steady high, which the plan above keeps
// representable by never choosing a top of 65535.
static unsigned char mm_pico_pwm_members[NUM_PWM_SLICES];
static unsigned char mm_pico_pwm_comparator_claimed[NUM_PWM_SLICES][2];

unsigned long mm_pico_mcu_system_clock_hz(void) {
    return (unsigned long)clock_get_hz(clk_sys);
}

unsigned int mm_pico_mcu_pwm_slice(unsigned int pin) {
    return mm_pico_pin_valid(pin) ? (unsigned int)pwm_gpio_to_slice_num(pin) : 0u;
}

unsigned int mm_pico_mcu_pwm_comparator(unsigned int pin) {
    return mm_pico_pin_valid(pin) ? (unsigned int)pwm_gpio_to_channel(pin) : 0u;
}

int mm_pico_mcu_pwm_configure(unsigned int pin, unsigned int top, unsigned int divider_x16) {
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_BAD_ARGUMENT;
    if (top > 65534u || divider_x16 < 16u || divider_x16 > 4095u) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_pin_owner[pin] == MM_PICO_OWNER_PWM) return MM_PICO_MCU_OK;
    if (mm_pico_pin_owner[pin] == MM_PICO_OWNER_WATCHED ||
        mm_pico_pin_owner[pin] == MM_PICO_OWNER_ADC ||
        mm_pico_pin_owner[pin] == MM_PICO_OWNER_I2S ||
        mm_pico_pin_owner[pin] == MM_PICO_OWNER_CAMERA)
        return MM_PICO_MCU_BUSY;
    const unsigned int slice = (unsigned int)pwm_gpio_to_slice_num(pin);
    const unsigned int comparator = (unsigned int)pwm_gpio_to_channel(pin);
    if (slice >= (unsigned int)NUM_PWM_SLICES) return MM_PICO_MCU_UNSUPPORTED;
    // Two GPIOs sixteen apart share a comparator; the second is an alias of
    // the first's signal, not a second output.
    if (mm_pico_pwm_comparator_claimed[slice][comparator]) return MM_PICO_MCU_BUSY;

    if (mm_pico_pwm_members[slice] == 0) {
        pwm_set_enabled(slice, false);
        pwm_set_clkdiv_int_frac4(slice, (uint8_t)(divider_x16 >> 4), (uint8_t)(divider_x16 & 15u));
        pwm_set_wrap(slice, (uint16_t)top);
        pwm_set_counter(slice, 0);
    }
    pwm_set_chan_level(slice, comparator, 0);
    gpio_set_function(pin, GPIO_FUNC_PWM);
    if (mm_pico_pwm_members[slice] == 0) pwm_set_enabled(slice, true);
    mm_pico_pwm_members[slice]++;
    mm_pico_pwm_comparator_claimed[slice][comparator] = 1;
    mm_pico_pin_owner[pin] = MM_PICO_OWNER_PWM;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_pwm_write(unsigned int pin, unsigned int level) {
    if (!mm_pico_pin_valid(pin) || level > 65535u) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_pin_owner[pin] != MM_PICO_OWNER_PWM) return MM_PICO_MCU_BAD_ARGUMENT;
    pwm_set_gpio_level(pin, (uint16_t)level);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_pwm_release(unsigned int pin) {
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_pin_owner[pin] != MM_PICO_OWNER_PWM) return MM_PICO_MCU_OK;
    const unsigned int slice = (unsigned int)pwm_gpio_to_slice_num(pin);
    const unsigned int comparator = (unsigned int)pwm_gpio_to_channel(pin);
    pwm_set_chan_level(slice, comparator, 0);
    gpio_deinit(pin);
    mm_pico_pwm_comparator_claimed[slice][comparator] = 0;
    mm_pico_pin_owner[pin] = MM_PICO_OWNER_NONE;
    if (mm_pico_pwm_members[slice] > 0 && --mm_pico_pwm_members[slice] == 0)
        pwm_set_enabled(slice, false);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_ticks_ms(unsigned long* ticks) {
    if (ticks == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *ticks = (unsigned long)to_ms_since_boot(get_absolute_time());
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_delay_us(unsigned long microseconds) {
    sleep_us(microseconds);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_ticks_us(unsigned long* ticks) {
    if (ticks == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *ticks = (unsigned long)to_us_since_boot(get_absolute_time());
    return MM_PICO_MCU_OK;
}

// I2S over PIO and DMA. One link, instance zero, the RP2350's and RP2040's
// only form of I2S, since neither has the peripheral.
//
// A clock state machine is the link's master: it drives the bit clock and the
// word clock by side-set -- which is why the word clock must be the GPIO after
// the bit clock -- and shifts the transmit line out, two PIO cycles a bit.
// It runs from configure to release, fed by a pair of DMA channels chained in
// ping-pong from two blocks the adapter owns, so the clocks never stop and a
// DAC deriving its own clocks from them never relocks. A receive state machine,
// when the link has a receive line, samples it on each bit clock rising edge
// after aligning to the word clock, and feeds a second ping-pong pair.
//
// Between the blocks and the caller sit two rings the adapter also owns, one
// per direction, in PIO words: a frame is one word with sixteen-bit slots,
// left slot high, and two words, left then right, with thirty-two-bit slots.
// The DMA interrupt handler does the transport's bounded work and nothing
// else: on each finished block it counts what the block carried, refills it
// from the transmit ring -- with zeros for any frame the ring cannot supply,
// counted as missed while the direction is started -- or empties it into the
// receive ring, dropping and counting what does not fit, and re-arms the
// channel. The calls below touch the rings only with interrupts masked, so
// the handler and they never see a ring half-updated.

#define MM_PICO_I2S_BLOCK_FRAMES 32u
#define MM_PICO_I2S_RING_FRAMES 256u
#define MM_PICO_I2S_MOST_WORDS 2u

typedef struct {
    uint32_t block[2][MM_PICO_I2S_BLOCK_FRAMES * MM_PICO_I2S_MOST_WORDS];
    uint32_t ring[MM_PICO_I2S_RING_FRAMES * MM_PICO_I2S_MOST_WORDS];
    unsigned int head;          // frames, next to write
    unsigned int tail;          // frames, next to read
    unsigned int count;         // frames in the ring
    unsigned int block_real[2]; // transmit: ring frames in each block
    unsigned int block_missed[2];
    int channel[2];
    int started;
    uint64_t completed;
    uint32_t missed;
} mm_pico_i2s_lane_t;

typedef struct {
    int configured;
    unsigned int bit_clock;
    unsigned int word_clock;
    int has_transmit;
    unsigned int transmit;
    int has_receive;
    unsigned int receive;
    unsigned long rate_hz;
    unsigned int slot_bits;
    unsigned int words_per_frame;
    uint32_t divider_x256;
    PIO clock_pio;
    unsigned int clock_sm;
    unsigned int clock_offset;
    PIO receive_pio;
    unsigned int receive_sm;
    unsigned int receive_offset;
    uint16_t clock_instructions[8];
    uint16_t receive_instructions[7];
    pio_program_t clock_program;
    pio_program_t receive_program;
    mm_pico_i2s_lane_t out;
    mm_pico_i2s_lane_t in;
} mm_pico_i2s_link_t;

static mm_pico_i2s_link_t mm_pico_i2s;
static int mm_pico_i2s_handler_ready;

static unsigned int mm_pico_i2s_block_words(void) {
    return MM_PICO_I2S_BLOCK_FRAMES * mm_pico_i2s.words_per_frame;
}

// The clock program, for a slot of slot_bits: each slot is slot_bits - 1 bits
// in its own half of the word clock and its last bit in the first bit clock of
// the other half, which is I2S's one-bit delay. Left is the low half.
static void mm_pico_i2s_build_clock_program(unsigned int slot_bits) {
    const unsigned int count = slot_bits - 2u;
    uint16_t* p = mm_pico_i2s.clock_instructions;
    p[0] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0u));
    p[1] = (uint16_t)(pio_encode_jmp_x_dec(0) | pio_encode_sideset(2, 1u));
    p[2] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2u));
    p[3] = (uint16_t)(pio_encode_set(pio_x, count) | pio_encode_sideset(2, 3u));
    p[4] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2u));
    p[5] = (uint16_t)(pio_encode_jmp_x_dec(4) | pio_encode_sideset(2, 3u));
    p[6] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0u));
    p[7] = (uint16_t)(pio_encode_set(pio_x, count) | pio_encode_sideset(2, 1u));
    mm_pico_i2s.clock_program.instructions = mm_pico_i2s.clock_instructions;
    mm_pico_i2s.clock_program.length = 8;
    mm_pico_i2s.clock_program.origin = -1;
}

// The receive program: find the start of a left slot -- the word clock
// falling -- skip the right slot's delayed last bit, then sample on every bit
// clock rising edge. Autopush packs frames exactly as the clock program
// unpacks them.
static void mm_pico_i2s_build_receive_program(void) {
    const unsigned int bit_clock = mm_pico_i2s.bit_clock;
    const unsigned int word_clock = mm_pico_i2s.word_clock;
    uint16_t* p = mm_pico_i2s.receive_instructions;
    p[0] = (uint16_t)pio_encode_wait_gpio(true, word_clock);
    p[1] = (uint16_t)pio_encode_wait_gpio(false, word_clock);
    p[2] = (uint16_t)pio_encode_wait_gpio(false, bit_clock);
    p[3] = (uint16_t)pio_encode_wait_gpio(true, bit_clock);
    p[4] = (uint16_t)pio_encode_wait_gpio(false, bit_clock);
    p[5] = (uint16_t)pio_encode_wait_gpio(true, bit_clock);
    p[6] = (uint16_t)pio_encode_in(pio_pins, 1);
    mm_pico_i2s.receive_program.instructions = mm_pico_i2s.receive_instructions;
    mm_pico_i2s.receive_program.length = 7;
    mm_pico_i2s.receive_program.origin = -1;
}

// One transmit block from the ring, zeros for what it lacks.
static void mm_pico_i2s_fill(unsigned int b) {
    mm_pico_i2s_lane_t* lane = &mm_pico_i2s.out;
    const unsigned int wpf = mm_pico_i2s.words_per_frame;
    unsigned int real = 0;
    for (unsigned int f = 0; f < MM_PICO_I2S_BLOCK_FRAMES; ++f) {
        uint32_t* word = &lane->block[b][f * wpf];
        if (lane->started && lane->count > 0) {
            const uint32_t* from = &lane->ring[lane->tail * wpf];
            for (unsigned int w = 0; w < wpf; ++w) word[w] = from[w];
            lane->tail = (lane->tail + 1u) % MM_PICO_I2S_RING_FRAMES;
            lane->count--;
            real++;
        } else {
            for (unsigned int w = 0; w < wpf; ++w) word[w] = 0;
        }
    }
    lane->block_real[b] = real;
    lane->block_missed[b] = lane->started ? MM_PICO_I2S_BLOCK_FRAMES - real : 0u;
}

// One receive block into the ring, dropping and counting what does not fit.
static void mm_pico_i2s_drain(unsigned int b) {
    mm_pico_i2s_lane_t* lane = &mm_pico_i2s.in;
    if (!lane->started) return;
    const unsigned int wpf = mm_pico_i2s.words_per_frame;
    for (unsigned int f = 0; f < MM_PICO_I2S_BLOCK_FRAMES; ++f) {
        lane->completed++;
        if (lane->count == MM_PICO_I2S_RING_FRAMES) {
            lane->missed++;
            continue;
        }
        uint32_t* to = &lane->ring[lane->head * wpf];
        const uint32_t* word = &lane->block[b][f * wpf];
        for (unsigned int w = 0; w < wpf; ++w) to[w] = word[w];
        lane->head = (lane->head + 1u) % MM_PICO_I2S_RING_FRAMES;
        lane->count++;
    }
}

static void mm_pico_i2s_dma_handler(void) {
    if (!mm_pico_i2s.configured) return;
    for (unsigned int b = 0; b < 2; ++b) {
        const int out_channel = mm_pico_i2s.out.channel[b];
        if (out_channel >= 0 && dma_channel_get_irq0_status((uint)out_channel)) {
            dma_channel_acknowledge_irq0((uint)out_channel);
            mm_pico_i2s_lane_t* lane = &mm_pico_i2s.out;
            lane->completed += lane->block_real[b];
            lane->missed += lane->block_missed[b];
            mm_pico_i2s_fill(b);
            dma_channel_set_read_addr((uint)out_channel, lane->block[b], false);
        }
        const int in_channel = mm_pico_i2s.in.channel[b];
        if (in_channel >= 0 && dma_channel_get_irq0_status((uint)in_channel)) {
            dma_channel_acknowledge_irq0((uint)in_channel);
            mm_pico_i2s_drain(b);
            dma_channel_set_write_addr((uint)in_channel, mm_pico_i2s.in.block[b], false);
        }
    }
}

// Two channels, each chained to the other, each raising the interrupt when its
// block is done. Neither is triggered here.
static int mm_pico_i2s_claim_pair(mm_pico_i2s_lane_t* lane, int transmit, PIO pio,
                                  unsigned int sm) {
    lane->channel[0] = dma_claim_unused_channel(false);
    lane->channel[1] = dma_claim_unused_channel(false);
    if (lane->channel[0] < 0 || lane->channel[1] < 0) return 0;
    const unsigned int words = mm_pico_i2s_block_words();
    for (unsigned int b = 0; b < 2; ++b) {
        const uint channel = (uint)lane->channel[b];
        dma_channel_config config = dma_channel_get_default_config(channel);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
        channel_config_set_read_increment(&config, transmit != 0);
        channel_config_set_write_increment(&config, transmit == 0);
        channel_config_set_dreq(&config, pio_get_dreq(pio, sm, transmit != 0));
        channel_config_set_chain_to(&config, (uint)lane->channel[1u - b]);
        if (transmit)
            dma_channel_configure(channel, &config, &pio->txf[sm], lane->block[b], words, false);
        else
            dma_channel_configure(channel, &config, lane->block[b], &pio->rxf[sm], words, false);
        dma_channel_set_irq0_enabled(channel, true);
    }
    return 1;
}

// Chained channels are unchained and disabled before they are aborted, so
// that neither can re-trigger the other mid-abort.
static void mm_pico_i2s_free_pair(mm_pico_i2s_lane_t* lane) {
    for (unsigned int b = 0; b < 2; ++b) {
        if (lane->channel[b] < 0) continue;
        const uint channel = (uint)lane->channel[b];
        dma_channel_set_irq0_enabled(channel, false);
        hw_clear_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    }
    for (unsigned int b = 0; b < 2; ++b) {
        if (lane->channel[b] < 0) continue;
        const uint channel = (uint)lane->channel[b];
        dma_channel_abort(channel);
        dma_channel_acknowledge_irq0(channel);
        dma_channel_unclaim(channel);
        lane->channel[b] = -1;
    }
}

static int mm_pico_i2s_same(unsigned int bit_clock, unsigned int word_clock, int has_transmit,
                            unsigned int transmit, int has_receive, unsigned int receive,
                            unsigned long rate_hz, unsigned int slot_bits) {
    const mm_pico_i2s_link_t* l = &mm_pico_i2s;
    return l->bit_clock == bit_clock && l->word_clock == word_clock &&
           l->has_transmit == has_transmit && (!has_transmit || l->transmit == transmit) &&
           l->has_receive == has_receive && (!has_receive || l->receive == receive) &&
           l->rate_hz == rate_hz && l->slot_bits == slot_bits;
}

static void mm_pico_i2s_release_all(void) {
    mm_pico_i2s_link_t* l = &mm_pico_i2s;
    if (l->clock_pio != NULL) pio_sm_set_enabled(l->clock_pio, l->clock_sm, false);
    if (l->receive_pio != NULL) pio_sm_set_enabled(l->receive_pio, l->receive_sm, false);
    mm_pico_i2s_free_pair(&l->out);
    mm_pico_i2s_free_pair(&l->in);
    if (l->clock_pio != NULL)
        pio_remove_program_and_unclaim_sm(&l->clock_program, l->clock_pio, l->clock_sm,
                                          l->clock_offset);
    if (l->receive_pio != NULL)
        pio_remove_program_and_unclaim_sm(&l->receive_program, l->receive_pio,
                                          l->receive_sm, l->receive_offset);
    const unsigned int pins[4] = {l->bit_clock, l->word_clock, l->transmit, l->receive};
    const int used[4] = {1, 1, l->has_transmit, l->has_receive};
    for (unsigned int i = 0; i < 4; ++i) {
        if (!used[i] || !l->configured) continue;
        gpio_deinit(pins[i]);
        mm_pico_pin_owner[pins[i]] = MM_PICO_OWNER_NONE;
    }
    l->configured = 0;
    l->clock_pio = NULL;
    l->receive_pio = NULL;
}

int mm_pico_mcu_i2s_configure(unsigned int instance, unsigned int bit_clock,
                              unsigned int word_clock, int has_transmit, unsigned int transmit,
                              int has_receive, unsigned int receive, unsigned long rate_hz,
                              unsigned int slot_bits) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (rate_hz == 0 || (slot_bits != 16 && slot_bits != 32) ||
        (!has_transmit && !has_receive))
        return MM_PICO_MCU_BAD_ARGUMENT;
    const unsigned int pins[4] = {bit_clock, word_clock, transmit, receive};
    const int used[4] = {1, 1, has_transmit, has_receive};
    for (unsigned int i = 0; i < 4; ++i) {
        if (!used[i]) continue;
        if (!mm_pico_pin_valid(pins[i]) || pins[i] > 31u) return MM_PICO_MCU_BAD_ARGUMENT;
        for (unsigned int j = 0; j < i; ++j)
            if (used[j] && pins[j] == pins[i]) return MM_PICO_MCU_BAD_ARGUMENT;
    }
    // Side-set drives the two clocks from consecutive pins.
    if (word_clock != bit_clock + 1u) return MM_PICO_MCU_BAD_ARGUMENT;
    if (mm_pico_i2s.configured)
        return mm_pico_i2s_same(bit_clock, word_clock, has_transmit, transmit, has_receive,
                                receive, rate_hz, slot_bits)
                   ? MM_PICO_MCU_OK
                   : MM_PICO_MCU_BUSY;
    for (unsigned int i = 0; i < 4; ++i)
        if (used[i] && (mm_pico_gpio_watched[pins[i]] || mm_pico_analog_holds(pins[i])))
            return MM_PICO_MCU_BUSY;

    // Two PIO cycles a bit, two slots a frame. The divider has eight fraction
    // bits and must be at least one.
    const uint64_t cycles_per_frame = 4ull * slot_bits;
    const uint64_t clock = (uint64_t)clock_get_hz(clk_sys);
    const uint64_t divider_x256 =
        (clock * 256ull + rate_hz * cycles_per_frame / 2ull) / (rate_hz * cycles_per_frame);
    if (divider_x256 < 256ull || divider_x256 > 65535ull * 256ull + 255ull)
        return MM_PICO_MCU_BAD_ARGUMENT;

    mm_pico_i2s_link_t* l = &mm_pico_i2s;
    l->bit_clock = bit_clock;
    l->word_clock = word_clock;
    l->has_transmit = has_transmit;
    l->transmit = transmit;
    l->has_receive = has_receive;
    l->receive = receive;
    l->rate_hz = rate_hz;
    l->slot_bits = slot_bits;
    l->words_per_frame = slot_bits == 16 ? 1u : 2u;
    l->divider_x256 = (uint32_t)divider_x256;
    l->out.channel[0] = l->out.channel[1] = -1;
    l->in.channel[0] = l->in.channel[1] = -1;
    l->out.started = l->in.started = 0;
    l->out.head = l->out.tail = l->out.count = 0;
    l->in.head = l->in.tail = l->in.count = 0;
    l->clock_pio = NULL;
    l->receive_pio = NULL;

    mm_pico_i2s_build_clock_program(slot_bits);
    PIO pio;
    uint sm;
    uint offset;
    if (!pio_claim_free_sm_and_add_program(&l->clock_program, &pio, &sm, &offset))
        return MM_PICO_MCU_BUSY;
    l->clock_pio = pio;
    l->clock_sm = sm;
    l->clock_offset = offset;
    if (has_receive) {
        mm_pico_i2s_build_receive_program();
        if (!pio_claim_free_sm_and_add_program(&l->receive_program, &pio, &sm, &offset)) {
            pio_remove_program_and_unclaim_sm(&l->clock_program, l->clock_pio, l->clock_sm,
                                              l->clock_offset);
            l->clock_pio = NULL;
            return MM_PICO_MCU_BUSY;
        }
        l->receive_pio = pio;
        l->receive_sm = sm;
        l->receive_offset = offset;
    }
    if (!mm_pico_i2s_claim_pair(&l->out, 1, l->clock_pio, l->clock_sm) ||
        (has_receive && !mm_pico_i2s_claim_pair(&l->in, 0, l->receive_pio, l->receive_sm))) {
        l->configured = 0;
        mm_pico_i2s_release_all();
        return MM_PICO_MCU_BUSY;
    }

    // The clock state machine: side-set on the clocks, out on the transmit
    // line when there is one, autopull a word at a time, most significant bit
    // first, the transmit FIFO doubled.
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, l->clock_offset, l->clock_offset + 7u);
    sm_config_set_sideset(&c, 2, false, false);
    sm_config_set_sideset_pins(&c, bit_clock);
    if (has_transmit)
        sm_config_set_out_pins(&c, transmit, 1);
    else
        sm_config_set_out_pins(&c, bit_clock, 0);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac8(&c, (uint32_t)(divider_x256 >> 8), (uint8_t)(divider_x256 & 255u));
    pio_gpio_init(l->clock_pio, bit_clock);
    pio_gpio_init(l->clock_pio, word_clock);
    pio_sm_set_consecutive_pindirs(l->clock_pio, l->clock_sm, bit_clock, 2, true);
    if (has_transmit) {
        pio_gpio_init(l->clock_pio, transmit);
        pio_sm_set_consecutive_pindirs(l->clock_pio, l->clock_sm, transmit, 1, true);
    }
    pio_sm_init(l->clock_pio, l->clock_sm, l->clock_offset + 7u, &c);

    if (has_receive) {
        pio_sm_config r = pio_get_default_sm_config();
        sm_config_set_wrap(&r, l->receive_offset + 4u, l->receive_offset + 6u);
        sm_config_set_in_pins(&r, receive);
        sm_config_set_in_shift(&r, false, true, 32);
        sm_config_set_fifo_join(&r, PIO_FIFO_JOIN_RX);
        pio_gpio_init(l->receive_pio, receive);
        pio_sm_set_consecutive_pindirs(l->receive_pio, l->receive_sm, receive, 1, false);
        pio_sm_init(l->receive_pio, l->receive_sm, l->receive_offset, &r);
    }

    for (unsigned int i = 0; i < 4; ++i)
        if (used[i]) mm_pico_pin_owner[pins[i]] = MM_PICO_OWNER_I2S;

    const uint32_t saved = save_and_disable_interrupts();
    l->configured = 1;
    mm_pico_i2s_fill(0);
    mm_pico_i2s_fill(1);
    l->out.block_real[0] = l->out.block_real[1] = 0;
    l->out.block_missed[0] = l->out.block_missed[1] = 0;
    restore_interrupts(saved);
    if (!mm_pico_i2s_handler_ready) {
        irq_add_shared_handler(DMA_IRQ_0, mm_pico_i2s_dma_handler,
                               PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_0, true);
        mm_pico_i2s_handler_ready = 1;
    }
    // The receiver first, so it is waiting for the word clock when the clock
    // state machine starts it; the transmit DMA before the clock state
    // machine, so its FIFO is full when the first bit is shifted.
    if (has_receive) {
        dma_channel_start((uint)l->in.channel[0]);
        pio_sm_set_enabled(l->receive_pio, l->receive_sm, true);
    }
    dma_channel_start((uint)l->out.channel[0]);
    pio_sm_set_enabled(l->clock_pio, l->clock_sm, true);
    return MM_PICO_MCU_OK;
}

// hertz = clk_sys * 256 / (divider_x256 * cycles per frame).
int mm_pico_mcu_i2s_rate(unsigned int instance, unsigned long long* numerator,
                         unsigned long long* denominator) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_i2s.configured) return MM_PICO_MCU_BAD_ARGUMENT;
    if (numerator == NULL || denominator == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *numerator = (unsigned long long)clock_get_hz(clk_sys) * 256ull;
    *denominator = (unsigned long long)mm_pico_i2s.divider_x256 * 4ull * mm_pico_i2s.slot_bits;
    return MM_PICO_MCU_OK;
}

static mm_pico_i2s_lane_t* mm_pico_i2s_lane(unsigned int instance, int receive) {
    if (instance != 0 || !mm_pico_i2s.configured) return NULL;
    if (receive) return mm_pico_i2s.has_receive ? &mm_pico_i2s.in : NULL;
    return mm_pico_i2s.has_transmit ? &mm_pico_i2s.out : NULL;
}

int mm_pico_mcu_i2s_start(unsigned int instance, int receive) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_i2s_lane_t* lane = mm_pico_i2s_lane(instance, receive);
    if (lane == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    const uint32_t saved = save_and_disable_interrupts();
    if (receive) lane->head = lane->tail = lane->count = 0;
    lane->completed = 0;
    lane->missed = 0;
    lane->block_real[0] = lane->block_real[1] = 0;
    lane->block_missed[0] = lane->block_missed[1] = 0;
    lane->started = 1;
    restore_interrupts(saved);
    return MM_PICO_MCU_OK;
}

// Frames into the transmit ring, as many as fit.
int mm_pico_mcu_i2s_write(unsigned int instance, const uint32_t* words, size_t frames,
                          size_t* accepted) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_i2s_lane_t* lane = mm_pico_i2s_lane(instance, 0);
    if (lane == NULL || accepted == NULL || (words == NULL && frames != 0))
        return MM_PICO_MCU_BAD_ARGUMENT;
    const unsigned int wpf = mm_pico_i2s.words_per_frame;
    const uint32_t saved = save_and_disable_interrupts();
    size_t moved = 0;
    while (moved < frames && lane->count < MM_PICO_I2S_RING_FRAMES) {
        uint32_t* to = &lane->ring[lane->head * wpf];
        for (unsigned int w = 0; w < wpf; ++w) to[w] = words[moved * wpf + w];
        lane->head = (lane->head + 1u) % MM_PICO_I2S_RING_FRAMES;
        lane->count++;
        moved++;
    }
    restore_interrupts(saved);
    *accepted = moved;
    return MM_PICO_MCU_OK;
}

// Frames out of the receive ring, as many as there are and fit.
int mm_pico_mcu_i2s_read(unsigned int instance, uint32_t* words, size_t frames,
                         size_t* count) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_i2s_lane_t* lane = mm_pico_i2s_lane(instance, 1);
    if (lane == NULL || count == NULL || (words == NULL && frames != 0))
        return MM_PICO_MCU_BAD_ARGUMENT;
    const unsigned int wpf = mm_pico_i2s.words_per_frame;
    const uint32_t saved = save_and_disable_interrupts();
    size_t moved = 0;
    while (moved < frames && lane->count > 0) {
        const uint32_t* from = &lane->ring[lane->tail * wpf];
        for (unsigned int w = 0; w < wpf; ++w) words[moved * wpf + w] = from[w];
        lane->tail = (lane->tail + 1u) % MM_PICO_I2S_RING_FRAMES;
        lane->count--;
        moved++;
    }
    restore_interrupts(saved);
    *count = moved;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_i2s_progress(unsigned int instance, int receive,
                             unsigned long long* completed, size_t* queued,
                             unsigned long* missed) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_i2s_lane_t* lane = mm_pico_i2s_lane(instance, receive);
    if (lane == NULL || completed == NULL || queued == NULL || missed == NULL)
        return MM_PICO_MCU_BAD_ARGUMENT;
    const uint32_t saved = save_and_disable_interrupts();
    *completed = lane->completed;
    *queued = lane->count + (receive ? 0u : lane->block_real[0] + lane->block_real[1]);
    *missed = lane->missed;
    restore_interrupts(saved);
    return MM_PICO_MCU_OK;
}

// The ring is emptied; a block already handed to the DMA still goes out,
// which is at most two blocks, and the transmitter is on zeros after that.
int mm_pico_mcu_i2s_stop(unsigned int instance, int receive) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_i2s_lane_t* lane = mm_pico_i2s_lane(instance, receive);
    if (lane == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    const uint32_t saved = save_and_disable_interrupts();
    lane->started = 0;
    lane->head = lane->tail = lane->count = 0;
    lane->block_real[0] = lane->block_real[1] = 0;
    lane->block_missed[0] = lane->block_missed[1] = 0;
    restore_interrupts(saved);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_i2s_release(unsigned int instance) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_i2s.configured) return MM_PICO_MCU_OK;
    const uint32_t saved = save_and_disable_interrupts();
    mm_pico_i2s.out.started = 0;
    mm_pico_i2s.in.started = 0;
    restore_interrupts(saved);
    mm_pico_i2s_release_all();
    return MM_PICO_MCU_OK;
}

// A one-wire pulse-width-coded output over PIO, the WS2812's transport. One
// state machine an instance runs four instructions, each bit taking t3 ticks
// low, t1 high, and then t2 more high for a one or low for a zero:
//
//   0: out x, 1     side 0 [t3 - 1]
//   1: jmp !x 3     side 1 [t1 - 1]
//   2: jmp 0        side 1 [t2 - 1]
//   3: nop          side 0 [t2 - 1]
//
// With the transmit FIFO empty the state machine stalls on the out, and the
// side-set has already taken the line low, so idle is low. Autopull takes a
// byte a word, most significant bit first. One side-set bit leaves four delay
// bits, so each of t1, t2, and t3 is one to sixteen ticks.

#define MM_PICO_PULSE_INSTANCES 2u
#define MM_PICO_PULSE_MOST_TICKS 16u
#define MM_PICO_PULSE_MOST_PERIOD_NS 10000000ul

typedef struct {
    int configured;
    unsigned int pin;
    unsigned long bit_period_ns;
    unsigned long zero_high_ns;
    unsigned long one_high_ns;
    unsigned long reset_ns;
    PIO pio;
    unsigned int sm;
    unsigned int offset;
    uint16_t instructions[4];
    pio_program_t program;
} mm_pico_pulse_t;

static mm_pico_pulse_t mm_pico_pulse[MM_PICO_PULSE_INSTANCES];

static uint64_t mm_pico_pulse_distance(uint64_t a, uint64_t b) {
    return a > b ? a - b : b - a;
}

// Ticks per bit, n, and its split into t1, t2, and t3, choosing the n whose
// worse high time is nearest what was asked, ties to the fewer ticks; and
// the divider, in 256ths, that makes n ticks one period. Zero when no n fits
// the instruction delays and the divider's range.
static int mm_pico_pulse_plan(unsigned long period, unsigned long zero, unsigned long one,
                              unsigned int* t1, unsigned int* t2, unsigned int* t3,
                              uint32_t* divider_x256) {
    const uint64_t clock = (uint64_t)clock_get_hz(clk_sys);
    int found = 0;
    uint64_t best_error = 0;
    unsigned int best_n = 0;
    for (unsigned int n = 3; n <= 3u * MM_PICO_PULSE_MOST_TICKS; ++n) {
        const uint64_t a = ((uint64_t)zero * n + period / 2u) / period;
        const uint64_t b = ((uint64_t)one * n + period / 2u) / period;
        if (a < 1u || b <= a || b >= n) continue;
        if (a > MM_PICO_PULSE_MOST_TICKS || b - a > MM_PICO_PULSE_MOST_TICKS ||
            n - b > MM_PICO_PULSE_MOST_TICKS)
            continue;
        const uint64_t ns_per_tick_scale = (uint64_t)n * 1000000000ull;
        const uint64_t divider =
            (clock * period * 256ull + ns_per_tick_scale / 2u) / ns_per_tick_scale;
        if (divider < 256ull || divider > 65535ull * 256ull + 255ull) continue;
        // Errors in nanoseconds times n, compared across n by cross-multiplying.
        const uint64_t e0 = mm_pico_pulse_distance(a * period, (uint64_t)zero * n);
        const uint64_t e1 = mm_pico_pulse_distance(b * period, (uint64_t)one * n);
        const uint64_t error = e0 > e1 ? e0 : e1;
        if (!found || error * best_n < best_error * n) {
            found = 1;
            best_error = error;
            best_n = n;
            *t1 = (unsigned int)a;
            *t2 = (unsigned int)(b - a);
            *t3 = (unsigned int)(n - b);
            *divider_x256 = (uint32_t)divider;
        }
    }
    return found;
}

static void mm_pico_pulse_build_program(mm_pico_pulse_t* p, unsigned int t1, unsigned int t2,
                                        unsigned int t3) {
    uint16_t* i = p->instructions;
    i[0] = (uint16_t)(pio_encode_out(pio_x, 1) | pio_encode_sideset(1, 0u) |
                      pio_encode_delay(t3 - 1u));
    i[1] = (uint16_t)(pio_encode_jmp_not_x(3) | pio_encode_sideset(1, 1u) |
                      pio_encode_delay(t1 - 1u));
    i[2] = (uint16_t)(pio_encode_jmp(0) | pio_encode_sideset(1, 1u) |
                      pio_encode_delay(t2 - 1u));
    i[3] = (uint16_t)(pio_encode_nop() | pio_encode_sideset(1, 0u) |
                      pio_encode_delay(t2 - 1u));
    p->program.instructions = p->instructions;
    p->program.length = 4;
    p->program.origin = -1;
}

int mm_pico_mcu_pulse_configure(unsigned int instance, unsigned int pin,
                                unsigned long bit_period_ns, unsigned long zero_high_ns,
                                unsigned long one_high_ns, unsigned long reset_ns) {
    if (instance >= MM_PICO_PULSE_INSTANCES) return MM_PICO_MCU_UNSUPPORTED;
    if (!mm_pico_pin_valid(pin) || pin > 31u) return MM_PICO_MCU_BAD_ARGUMENT;
    if (zero_high_ns == 0 || zero_high_ns >= one_high_ns || one_high_ns >= bit_period_ns ||
        bit_period_ns > MM_PICO_PULSE_MOST_PERIOD_NS)
        return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_pulse_t* p = &mm_pico_pulse[instance];
    if (p->configured)
        return p->pin == pin && p->bit_period_ns == bit_period_ns &&
                       p->zero_high_ns == zero_high_ns && p->one_high_ns == one_high_ns &&
                       p->reset_ns == reset_ns
                   ? MM_PICO_MCU_OK
                   : MM_PICO_MCU_BUSY;
    if (mm_pico_gpio_watched[pin] || mm_pico_analog_holds(pin)) return MM_PICO_MCU_BUSY;

    unsigned int t1 = 0, t2 = 0, t3 = 0;
    uint32_t divider_x256 = 0;
    if (!mm_pico_pulse_plan(bit_period_ns, zero_high_ns, one_high_ns, &t1, &t2, &t3,
                            &divider_x256))
        return MM_PICO_MCU_BAD_ARGUMENT;

    mm_pico_pulse_build_program(p, t1, t2, t3);
    PIO pio;
    uint sm;
    uint offset;
    if (!pio_claim_free_sm_and_add_program(&p->program, &pio, &sm, &offset))
        return MM_PICO_MCU_BUSY;

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 3u);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac8(&c, divider_x256 >> 8, (uint8_t)(divider_x256 & 255u));
    pio_sm_set_pins_with_mask(pio, sm, 0u, 1u << pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_gpio_init(pio, pin);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    p->pin = pin;
    p->bit_period_ns = bit_period_ns;
    p->zero_high_ns = zero_high_ns;
    p->one_high_ns = one_high_ns;
    p->reset_ns = reset_ns;
    p->pio = pio;
    p->sm = sm;
    p->offset = offset;
    p->configured = 1;
    mm_pico_pin_owner[pin] = MM_PICO_OWNER_PULSE;
    return MM_PICO_MCU_OK;
}

// A byte a FIFO word. Once the FIFO is empty the state machine holds the last
// byte, at most eight bits from done, so the wait covers those and the reset
// time after them.
int mm_pico_mcu_pulse_write(unsigned int instance, const unsigned char* data, size_t size) {
    if (instance >= MM_PICO_PULSE_INSTANCES) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_pulse_t* p = &mm_pico_pulse[instance];
    if (!p->configured || (data == NULL && size != 0)) return MM_PICO_MCU_BAD_ARGUMENT;
    for (size_t i = 0; i < size; ++i)
        pio_sm_put_blocking(p->pio, p->sm, (uint32_t)data[i] << 24);
    while (!pio_sm_is_tx_fifo_empty(p->pio, p->sm)) tight_loop_contents();
    const uint64_t tail_ns = 8ull * p->bit_period_ns + p->reset_ns;
    busy_wait_us((tail_ns + 999ull) / 1000ull + 1ull);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_pulse_release(unsigned int instance) {
    if (instance >= MM_PICO_PULSE_INSTANCES) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_pulse_t* p = &mm_pico_pulse[instance];
    if (!p->configured) return MM_PICO_MCU_OK;
    pio_sm_set_enabled(p->pio, p->sm, false);
    pio_remove_program_and_unclaim_sm(&p->program, p->pio, p->sm, p->offset);
    gpio_deinit(p->pin);
    mm_pico_pin_owner[p->pin] = MM_PICO_OWNER_NONE;
    p->configured = 0;
    p->pio = NULL;
    return MM_PICO_MCU_OK;
}

// An SD card's native bus over PIO and DMA: mm.mcu's sdio facility.
//
// The design -- a state machine that clocks commands out and responses in
// with the clock as side-set, a second that clocks a written block out and
// reads the card's CRC status and busy, a third in another PIO block that
// follows the clock and captures read blocks for DMA, and CRC16 checked on
// every data line in software -- learned from carlk3's
// no-OS-FatFS-SD-SDIO-SPI-RPi-Pico (https://github.com/carlk3/
// no-OS-FatFS-SD-SDIO-SPI-RPi-Pico, Apache License 2.0), whose SDIO part
// derives from ZuluSCSI's firmware. No code is copied from either; the
// programs, framing, and CRC method below are this project's own, and
// docs/modules-sdcard.mdy records the acknowledgement.
//
// The clock is driven by whichever of the command and write state machines
// is running; the other is stalled on a PULL, and a stalled state machine
// writes nothing, so they share the pin. Every half period is DELAY + 1
// instruction cycles, so the bus clock is clk_sys / (2 * (DELAY + 1)) at a
// divider of one: 25 MHz at the RP2350's 150 MHz. Lines change on a falling
// edge, and a line is sampled by the instruction that makes the next falling
// edge, which, after the input synchroniser's two cycles, sees it most of a
// period after the card changed it.
//
// Command state machine, instructions 0 to 17, fed four words a command --
// bits to send less one, the 48-bit frame in two words, and a control word of
// response bits after the start bit less one (0: no response) and post clocks
// less one -- and three for a run of idle clocks:
//
//   0: pull block          side 0        11: in null, 1       side 1 [D]
//   1: out x, 32           side 0        12: in pins, 1       side 0 [D]
//   2: set pindirs, 1      side 0        13: jmp y-- 12       side 1 [D]
//   3: out pins, 1         side 0 [D]    14: push block       side 0 [D]
//   4: jmp x-- 3           side 1 [D]    15: out x, 16        side 0
//   5: set pindirs, 0      side 0 [D]    16: nop              side 1 [D]
//   6: pull block          side 1 [D]    17: jmp x-- 16       side 0 [D]
//   7: out y, 16           side 0 [D]
//   8: jmp !y 15           side 1 [D]
//   9: nop                 side 1 [D]
//  10: jmp pin 9           side 0 [D]
//
// Write state machine, instructions 18 to 31, fed a nibble count less one and
// the block's nibbles -- start, data, the CRC16s, end -- and pushing the CRC
// status token, start bit excluded, once the card has sent it:
//
//  18: pull block          side 0        25: jmp pin 24       side 0 [D]
//  19: out x, 32           side 0        26: set y, 3         side 1 [D]
//  20: set pindirs, 15     side 0        27: in pins, 1       side 0 [D]
//  21: out pins, 4         side 0 [D]    28: jmp y-- 27       side 1 [D]
//  22: jmp x-- 21          side 1 [D]    29: push block       side 0 [D]
//  23: set pindirs, 0      side 0 [D]    30: jmp pin 18       side 1 [D]
//  24: nop                 side 1 [D]    31: jmp 30           side 0 [D]
//
// Read state machine, in another PIO block, fed a nibble count less one; it
// waits for a block's start bit on D0 and pushes its nibbles, the CRC16s
// included, sampling three cycles after each rising edge it sees. A rising
// edge counts only if CLK is still high two cycles later: a glitch on CLK
// as the card switches its data lines -- the start bit switches all four at
// once -- otherwise counts as an edge and shifts the block by a nibble.
//
//   0: pull block                         7: wait 0 pin CLK
//   1: mov y, osr                         8: wait 1 pin CLK [1]
//   2: mov x, y                           9: wait 1 pin CLK
//   3: wait 0 pin CLK                    10: in pins, 4
//   4: wait 1 pin CLK [1]                11: jmp x-- 7
//   5: wait 1 pin CLK                    12: jmp 2
//   6: jmp pin 3
//
// CLK is reached by WAIT PIN as an offset from the IN base, D0, modulo 32,
// which names the same pad whatever the PIO block's GPIO base.
//
// The four CRC16s of a 4-bit block are one CRC: interleaving four streams is
// substituting x^4 for x, so the CRC of the data bytes, most significant bit
// first, under G(x^4) = x^64 + x^48 + x^20 + 1 is the four lines' CRC16s
// interleaved a nibble a bit, which is exactly how they are sent. It is
// computed a nibble at a time from a sixteen-entry table.
//
// The adapter is linked into every Pico program, so nothing here holds more
// static RAM than it must: a read's blocks go by DMA straight into the
// caller's buffer, byte-swapped by the DMA engine, while a second channel,
// chained to and from the first, collects each block's two CRC words; and a
// written block's nibble stream is computed a word at a time as it is fed.

#define MM_PICO_SDIO_DELAY 2u
#define MM_PICO_SDIO_BLOCK 512u
#define MM_PICO_SDIO_MOST_READ_BLOCKS 8u
#define MM_PICO_SDIO_READ_WORDS 130u    // 1040 nibbles: 1024 data, 16 CRC
#define MM_PICO_SDIO_DATA_WORDS 128u
#define MM_PICO_SDIO_WRITE_NIBBLES 1042u
#define MM_PICO_SDIO_IDENTIFY_HZ 400000ul
#define MM_PICO_SDIO_RESPONSE_US 20000u
#define MM_PICO_SDIO_DATA_US 1000000u
#define MM_PICO_SDIO_BUSY_US 1000000u
#define MM_PICO_SDIO_WRITE_BASE 18u

typedef struct {
    int configured;
    unsigned int clock_pin;
    unsigned int command_pin;
    unsigned int data0_pin;
    PIO pio;
    unsigned int command_sm;
    unsigned int write_sm;
    unsigned int offset;
    PIO read_pio;
    unsigned int read_sm;
    unsigned int read_offset;
    int dma;
    int crc_dma;
    uint16_t instructions[32];
    uint16_t read_instructions[13];
    pio_program_t program;
    pio_program_t read_program;
} mm_pico_sdio_t;

static mm_pico_sdio_t mm_pico_sdio;
static uint32_t mm_pico_sdio_read_crcs[MM_PICO_SDIO_MOST_READ_BLOCKS * 2u];

// Why the last failed transfer failed, finer than its status: a diagnostic
// for hardware bring-up, read through mm_pico_sdio_last_failure.
static mm_pico_sdio_failure_t mm_pico_sdio_failure;
static unsigned int mm_pico_sdio_data_us;

static int mm_pico_sdio_fail(int status, unsigned int reason, unsigned int index,
                             uint32_t response, unsigned int words) {
    mm_pico_sdio_failure.reason = reason;
    mm_pico_sdio_failure.index = index;
    mm_pico_sdio_failure.response = response;
    mm_pico_sdio_failure.words = words;
    mm_pico_sdio_failure.data_us = mm_pico_sdio_data_us;
    const mm_pico_sdio_t* s = &mm_pico_sdio;
    mm_pico_sdio_failure.pc = (pio_sm_get_pc(s->pio, s->command_sm) - s->offset) & 31u;
    const uint32_t all = gpio_get_all();
    mm_pico_sdio_failure.pins = ((all >> s->clock_pin) & 1u) | (((all >> s->command_pin) & 1u) << 1) |
                                (((all >> s->data0_pin) & 15u) << 2);
    return status;
}

unsigned int mm_pico_sdio_last_data_us(void) { return mm_pico_sdio_data_us; }

void mm_pico_sdio_last_failure(mm_pico_sdio_failure_t* failure) {
    if (failure != NULL) *failure = mm_pico_sdio_failure;
}

static uint8_t mm_pico_sdio_crc7(const uint8_t* data, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t d = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (uint8_t)(crc << 1);
            if (((d ^ crc) & 0x80u) != 0) crc ^= 0x09u;
            d = (uint8_t)(d << 1);
        }
    }
    return (uint8_t)(crc & 0x7fu);
}

static uint64_t mm_pico_sdio_crc(const uint8_t* data, size_t size) {
    static const uint64_t table[16] = {
        0x0000000000000000ull, 0x0001000000100001ull, 0x0002000000200002ull,
        0x0003000000300003ull, 0x0004000000400004ull, 0x0005000000500005ull,
        0x0006000000600006ull, 0x0007000000700007ull, 0x0008000000800008ull,
        0x0009000000900009ull, 0x000a000000a0000aull, 0x000b000000b0000bull,
        0x000c000000c0000cull, 0x000d000000d0000dull, 0x000e000000e0000eull,
        0x000f000000f0000full};
    uint64_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc = (crc << 4) ^ table[(unsigned int)(crc >> 60) ^ (data[i] >> 4)];
        crc = (crc << 4) ^ table[(unsigned int)(crc >> 60) ^ (data[i] & 0xfu)];
    }
    return crc;
}

static void mm_pico_sdio_build(mm_pico_sdio_t* s) {
    const unsigned int d = MM_PICO_SDIO_DELAY;
    uint16_t* i = s->instructions;
#define MM_SS(v) pio_encode_sideset(1, (v))
    i[0] = (uint16_t)(pio_encode_pull(false, true) | MM_SS(0));
    i[1] = (uint16_t)(pio_encode_out(pio_x, 32) | MM_SS(0));
    i[2] = (uint16_t)(pio_encode_set(pio_pindirs, 1) | MM_SS(0));
    i[3] = (uint16_t)(pio_encode_out(pio_pins, 1) | MM_SS(0) | pio_encode_delay(d));
    i[4] = (uint16_t)(pio_encode_jmp_x_dec(3) | MM_SS(1) | pio_encode_delay(d));
    i[5] = (uint16_t)(pio_encode_set(pio_pindirs, 0) | MM_SS(0) | pio_encode_delay(d));
    i[6] = (uint16_t)(pio_encode_pull(false, true) | MM_SS(1) | pio_encode_delay(d));
    i[7] = (uint16_t)(pio_encode_out(pio_y, 16) | MM_SS(0) | pio_encode_delay(d));
    i[8] = (uint16_t)(pio_encode_jmp_not_y(15) | MM_SS(1) | pio_encode_delay(d));
    i[9] = (uint16_t)(pio_encode_nop() | MM_SS(1) | pio_encode_delay(d));
    i[10] = (uint16_t)(pio_encode_jmp_pin(9) | MM_SS(0) | pio_encode_delay(d));
    i[11] = (uint16_t)(pio_encode_in(pio_null, 1) | MM_SS(1) | pio_encode_delay(d));
    i[12] = (uint16_t)(pio_encode_in(pio_pins, 1) | MM_SS(0) | pio_encode_delay(d));
    i[13] = (uint16_t)(pio_encode_jmp_y_dec(12) | MM_SS(1) | pio_encode_delay(d));
    i[14] = (uint16_t)(pio_encode_push(false, true) | MM_SS(0) | pio_encode_delay(d));
    i[15] = (uint16_t)(pio_encode_out(pio_x, 16) | MM_SS(0));
    i[16] = (uint16_t)(pio_encode_nop() | MM_SS(1) | pio_encode_delay(d));
    i[17] = (uint16_t)(pio_encode_jmp_x_dec(16) | MM_SS(0) | pio_encode_delay(d));

    i[18] = (uint16_t)(pio_encode_pull(false, true) | MM_SS(0));
    i[19] = (uint16_t)(pio_encode_out(pio_x, 32) | MM_SS(0));
    i[20] = (uint16_t)(pio_encode_set(pio_pindirs, 15) | MM_SS(0));
    i[21] = (uint16_t)(pio_encode_out(pio_pins, 4) | MM_SS(0) | pio_encode_delay(d));
    i[22] = (uint16_t)(pio_encode_jmp_x_dec(21) | MM_SS(1) | pio_encode_delay(d));
    i[23] = (uint16_t)(pio_encode_set(pio_pindirs, 0) | MM_SS(0) | pio_encode_delay(d));
    i[24] = (uint16_t)(pio_encode_nop() | MM_SS(1) | pio_encode_delay(d));
    i[25] = (uint16_t)(pio_encode_jmp_pin(24) | MM_SS(0) | pio_encode_delay(d));
    i[26] = (uint16_t)(pio_encode_set(pio_y, 3) | MM_SS(1) | pio_encode_delay(d));
    i[27] = (uint16_t)(pio_encode_in(pio_pins, 1) | MM_SS(0) | pio_encode_delay(d));
    i[28] = (uint16_t)(pio_encode_jmp_y_dec(27) | MM_SS(1) | pio_encode_delay(d));
    i[29] = (uint16_t)(pio_encode_push(false, true) | MM_SS(0) | pio_encode_delay(d));
    i[30] = (uint16_t)(pio_encode_jmp_pin(MM_PICO_SDIO_WRITE_BASE) | MM_SS(1) | pio_encode_delay(d));
    i[31] = (uint16_t)(pio_encode_jmp(30) | MM_SS(0) | pio_encode_delay(d));
#undef MM_SS
    s->program.instructions = s->instructions;
    s->program.length = 32;
    s->program.origin = -1;

    const unsigned int clock = (s->clock_pin - s->data0_pin) & 31u;
    uint16_t* r = s->read_instructions;
    r[0] = (uint16_t)pio_encode_pull(false, true);
    r[1] = (uint16_t)pio_encode_mov(pio_y, pio_osr);
    r[2] = (uint16_t)pio_encode_mov(pio_x, pio_y);
    r[3] = (uint16_t)pio_encode_wait_pin(false, clock);
    r[4] = (uint16_t)(pio_encode_wait_pin(true, clock) | pio_encode_delay(1));
    r[5] = (uint16_t)pio_encode_jmp_pin(3);
    r[6] = (uint16_t)pio_encode_wait_pin(false, clock);
    r[7] = (uint16_t)(pio_encode_wait_pin(true, clock) | pio_encode_delay(1));
    r[8] = (uint16_t)pio_encode_in(pio_pins, 4);
    r[9] = (uint16_t)pio_encode_jmp_x_dec(6);
    r[10] = (uint16_t)pio_encode_jmp(2);
    s->read_program.instructions = s->read_instructions;
    s->read_program.length = 11;
    s->read_program.origin = -1;
}

// The bus clock's divider in 256ths, the smallest that keeps the clock at or
// under hz.
static uint32_t mm_pico_sdio_divider(unsigned long hz, unsigned long* actual) {
    const uint64_t cycles = 2ull * (MM_PICO_SDIO_DELAY + 1u);
    const uint64_t clock = (uint64_t)clock_get_hz(clk_sys);
    uint64_t divider = (clock * 256ull + cycles * hz - 1u) / (cycles * hz);
    if (divider < 256u) divider = 256u;
    if (divider > 65535ull * 256ull + 255ull) divider = 65535ull * 256ull + 255ull;
    *actual = (unsigned long)(clock * 256ull / (cycles * divider));
    return (uint32_t)divider;
}

static void mm_pico_sdio_set_divider(mm_pico_sdio_t* s, uint32_t divider) {
    pio_sm_set_clkdiv_int_frac8(s->pio, s->command_sm, divider >> 8, (uint8_t)(divider & 255u));
    pio_sm_set_clkdiv_int_frac8(s->pio, s->write_sm, divider >> 8, (uint8_t)(divider & 255u));
}

// Stops a state machine wherever it is and puts it back at its start with
// the clock low and its lines released.
static void mm_pico_sdio_reset(mm_pico_sdio_t* s, unsigned int sm, unsigned int start) {
    pio_sm_set_enabled(s->pio, sm, false);
    pio_sm_clear_fifos(s->pio, sm);
    pio_sm_restart(s->pio, sm);
    pio_sm_clkdiv_restart(s->pio, sm);
    pio_sm_exec(s->pio, sm, (uint)(pio_encode_set(pio_pindirs, 0) | pio_encode_sideset(1, 0)));
    pio_sm_exec(s->pio, sm, (uint)(pio_encode_jmp(s->offset + start) | pio_encode_sideset(1, 0)));
    pio_sm_set_enabled(s->pio, sm, true);
}

static void mm_pico_sdio_stop_read(mm_pico_sdio_t* s) {
    // Unchain before aborting, or the aborted channel's chain restarts the
    // other. A channel chained to itself is unchained; a CHAIN_TO of zero
    // would chain to channel 0.
    hw_write_masked(&dma_channel_hw_addr((uint)s->dma)->al1_ctrl,
                    (uint32_t)s->dma << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
                    DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    hw_write_masked(&dma_channel_hw_addr((uint)s->crc_dma)->al1_ctrl,
                    (uint32_t)s->crc_dma << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
                    DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    dma_channel_abort((uint)s->dma);
    dma_channel_abort((uint)s->crc_dma);
    pio_sm_set_enabled(s->read_pio, s->read_sm, false);
    pio_sm_clear_fifos(s->read_pio, s->read_sm);
    pio_sm_restart(s->read_pio, s->read_sm);
    pio_sm_exec(s->read_pio, s->read_sm, (uint)pio_encode_jmp(s->read_offset));
}

static int mm_pico_sdio_expired(uint64_t start, uint32_t limit_us) {
    return time_us_64() - start > limit_us;
}

// Waits until the command state machine has run everything queued and is
// stalled on its PULL again.
static int mm_pico_sdio_command_idle(mm_pico_sdio_t* s, uint32_t limit_us) {
    const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + s->command_sm);
    const uint64_t start = time_us_64();
    while (!pio_sm_is_tx_fifo_empty(s->pio, s->command_sm)) {
        if (mm_pico_sdio_expired(start, limit_us)) return 0;
    }
    s->pio->fdebug = stall;
    while ((s->pio->fdebug & stall) == 0) {
        if (mm_pico_sdio_expired(start, limit_us)) return 0;
    }
    return 1;
}

// count clocks, the command line driven high for the first.
static void mm_pico_sdio_queue_clocks(mm_pico_sdio_t* s, unsigned int count) {
    pio_sm_put_blocking(s->pio, s->command_sm, 0u);
    pio_sm_put_blocking(s->pio, s->command_sm, 0xffffffffu);
    pio_sm_put_blocking(s->pio, s->command_sm, (uint32_t)(count - 1u) & 0xffffu);
}

static void mm_pico_sdio_queue_command(mm_pico_sdio_t* s, unsigned int index, uint32_t argument,
                                       unsigned int response_bits, unsigned int post_clocks) {
    uint8_t frame[6] = {(uint8_t)(0x40u | (index & 0x3fu)), (uint8_t)(argument >> 24),
                        (uint8_t)(argument >> 16), (uint8_t)(argument >> 8),
                        (uint8_t)argument, 0};
    frame[5] = (uint8_t)((mm_pico_sdio_crc7(frame, 5) << 1) | 1u);
    pio_sm_put_blocking(s->pio, s->command_sm, 47u);
    pio_sm_put_blocking(s->pio, s->command_sm,
                        ((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16) |
                            ((uint32_t)frame[2] << 8) | frame[3]);
    pio_sm_put_blocking(s->pio, s->command_sm, ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16));
    const uint32_t after_start = response_bits == 0 ? 0u : response_bits - 2u;
    pio_sm_put_blocking(s->pio, s->command_sm,
                        (after_start << 16) | ((uint32_t)(post_clocks - 1u) & 0xffffu));
}

// The response's words as pushed: 48 bits in two, 136 in five, the last
// holding the remainder in its low bits.
static int mm_pico_sdio_take(mm_pico_sdio_t* s, uint32_t* words, unsigned int count,
                             unsigned int* taken) {
    while (*taken < count && !pio_sm_is_rx_fifo_empty(s->pio, s->command_sm))
        words[(*taken)++] = pio_sm_get(s->pio, s->command_sm);
    return *taken == count;
}

// Checks a short response's frame and answers its content, bits 39 to 8.
static int mm_pico_sdio_short(const uint32_t* raw, unsigned int index, int checked,
                              uint32_t* content) {
    const uint8_t bytes[5] = {(uint8_t)(raw[0] >> 24), (uint8_t)(raw[0] >> 16),
                              (uint8_t)(raw[0] >> 8), (uint8_t)raw[0], (uint8_t)(raw[1] >> 8)};
    if ((bytes[0] & 0xc0u) != 0) return MM_PICO_MCU_TRANSPORT_ERROR;    // start, transmission
    if ((raw[1] & 1u) == 0) return MM_PICO_MCU_TRANSPORT_ERROR;         // end bit
    if (checked) {
        if ((bytes[0] & 0x3fu) != (index & 0x3fu)) return MM_PICO_MCU_TRANSPORT_ERROR;
        if (mm_pico_sdio_crc7(bytes, 5) != ((raw[1] >> 1) & 0x7fu))
            return MM_PICO_MCU_TRANSPORT_ERROR;
    }
    *content = ((raw[0] & 0x00ffffffu) << 8) | ((raw[1] >> 8) & 0xffu);
    return MM_PICO_MCU_OK;
}

static int mm_pico_sdio_long(const uint32_t* raw, uint32_t* content) {
    if ((raw[0] >> 30) != 0 || (raw[4] & 1u) == 0) return MM_PICO_MCU_TRANSPORT_ERROR;
    for (int i = 0; i < 3; ++i) content[i] = (raw[i] << 8) | (raw[i + 1] >> 24);
    content[3] = (raw[3] << 8) | (raw[4] & 0xffu);
    uint8_t bytes[15];
    for (int i = 0; i < 15; ++i) bytes[i] = (uint8_t)(content[i / 4] >> (24 - 8 * (i % 4)));
    if (mm_pico_sdio_crc7(bytes, 15) != ((content[3] >> 1) & 0x7fu))
        return MM_PICO_MCU_TRANSPORT_ERROR;
    return MM_PICO_MCU_OK;
}

static int mm_pico_sdio_ready(unsigned int instance) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    return mm_pico_sdio.configured ? MM_PICO_MCU_OK : MM_PICO_MCU_BAD_ARGUMENT;
}

// D0 low is busy; it ends on its own, with clocks to help a card that wants
// them.
static int mm_pico_sdio_wait_busy(mm_pico_sdio_t* s) {
    const uint64_t start = time_us_64();
    while (!gpio_get(s->data0_pin)) {
        if (mm_pico_sdio_expired(start, MM_PICO_SDIO_BUSY_US))
            return mm_pico_sdio_fail(MM_PICO_MCU_TIMEOUT, MM_PICO_SDIO_FAIL_BUSY_TIMEOUT, 0, 0, 0);
        mm_pico_sdio_queue_clocks(s, 8);
        if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US)) {
            mm_pico_sdio_reset(s, s->command_sm, 0);
            return MM_PICO_MCU_TIMEOUT;
        }
    }
    return MM_PICO_MCU_OK;
}

static void mm_pico_sdio_claim_pins(const mm_pico_sdio_t* s, unsigned char owner) {
    mm_pico_pin_owner[s->clock_pin] = owner;
    mm_pico_pin_owner[s->command_pin] = owner;
    for (unsigned int i = 0; i < 4u; ++i) mm_pico_pin_owner[s->data0_pin + i] = owner;
}

int mm_pico_mcu_sdio_configure(unsigned int instance, unsigned int clock_pin,
                               unsigned int command_pin, unsigned int data0_pin,
                               unsigned int width) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    if (width != 4u) return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    if (s->configured)
        return s->clock_pin == clock_pin && s->command_pin == command_pin &&
                       s->data0_pin == data0_pin
                   ? MM_PICO_MCU_OK
                   : MM_PICO_MCU_BUSY;
    const unsigned int pins[6] = {clock_pin, command_pin, data0_pin, data0_pin + 1u,
                                  data0_pin + 2u, data0_pin + 3u};
    unsigned int lowest = pins[0], highest = pins[0];
    for (unsigned int i = 0; i < 6u; ++i) {
        if (!mm_pico_pin_valid(pins[i])) return MM_PICO_MCU_BAD_ARGUMENT;
        for (unsigned int j = 0; j < i; ++j)
            if (pins[i] == pins[j]) return MM_PICO_MCU_BAD_ARGUMENT;
        if (pins[i] < lowest) lowest = pins[i];
        if (pins[i] > highest) highest = pins[i];
    }
    if (highest - lowest > 31u) return MM_PICO_MCU_BAD_ARGUMENT;    // one PIO block's window
    for (unsigned int i = 0; i < 6u; ++i)
        if (mm_pico_gpio_watched[pins[i]] || mm_pico_analog_holds(pins[i]))
            return MM_PICO_MCU_BUSY;

    s->clock_pin = clock_pin;
    s->command_pin = command_pin;
    s->data0_pin = data0_pin;
    mm_pico_sdio_build(s);

    PIO pio;
    uint sm, offset;
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&s->program, &pio, &sm, &offset, lowest,
                                                          highest - lowest + 1u, true))
        return MM_PICO_MCU_BUSY;
    const int write_sm = pio_claim_unused_sm(pio, false);
    if (write_sm < 0) {
        pio_remove_program_and_unclaim_sm(&s->program, pio, sm, offset);
        return MM_PICO_MCU_BUSY;
    }
    PIO read_pio;
    uint read_sm, read_offset;
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&s->read_program, &read_pio, &read_sm,
                                                          &read_offset, lowest,
                                                          highest - lowest + 1u, true)) {
        pio_sm_unclaim(pio, (uint)write_sm);
        pio_remove_program_and_unclaim_sm(&s->program, pio, sm, offset);
        return MM_PICO_MCU_BUSY;
    }
    const int dma = dma_claim_unused_channel(false);
    const int crc_dma = dma < 0 ? -1 : dma_claim_unused_channel(false);
    if (crc_dma < 0) {
        if (dma >= 0) dma_channel_unclaim((uint)dma);
        pio_remove_program_and_unclaim_sm(&s->read_program, read_pio, read_sm, read_offset);
        pio_sm_unclaim(pio, (uint)write_sm);
        pio_remove_program_and_unclaim_sm(&s->program, pio, sm, offset);
        return MM_PICO_MCU_BUSY;
    }
    s->pio = pio;
    s->command_sm = sm;
    s->write_sm = (unsigned int)write_sm;
    s->offset = offset;
    s->read_pio = read_pio;
    s->read_sm = read_sm;
    s->read_offset = read_offset;
    s->dma = dma;
    s->crc_dma = crc_dma;

    for (unsigned int i = 0; i < 6u; ++i) {
        pio_gpio_init(pio, pins[i]);
        gpio_set_slew_rate(pins[i], GPIO_SLEW_RATE_FAST);
        if (pins[i] != clock_pin) gpio_pull_up(pins[i]);
        gpio_set_input_hysteresis_enabled(pins[i], true);
    }
    gpio_set_drive_strength(clock_pin, GPIO_DRIVE_STRENGTH_8MA);
    const uint64_t mask = (1ull << clock_pin) | (1ull << command_pin) | (0xfull << data0_pin);
    pio_set_input_sync_bypass_with_mask64(pio, mask, mask);
    pio_set_input_sync_bypass_with_mask64(read_pio, mask, mask);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 17u);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, clock_pin);
    sm_config_set_out_pins(&c, command_pin, 1);
    sm_config_set_set_pins(&c, command_pin, 1);
    sm_config_set_in_pins(&c, command_pin);
    sm_config_set_jmp_pin(&c, command_pin);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_in_shift(&c, false, true, 32);
    pio_sm_init(pio, sm, offset, &c);

    pio_sm_config w = pio_get_default_sm_config();
    sm_config_set_wrap(&w, offset + MM_PICO_SDIO_WRITE_BASE, offset + 31u);
    sm_config_set_sideset(&w, 1, false, false);
    sm_config_set_sideset_pins(&w, clock_pin);
    sm_config_set_out_pins(&w, data0_pin, 4);
    sm_config_set_set_pins(&w, data0_pin, 4);
    sm_config_set_in_pins(&w, data0_pin);
    sm_config_set_jmp_pin(&w, data0_pin);
    sm_config_set_out_shift(&w, false, true, 32);
    sm_config_set_in_shift(&w, false, false, 32);
    pio_sm_init(pio, (uint)write_sm, offset + MM_PICO_SDIO_WRITE_BASE, &w);

    pio_sm_config r = pio_get_default_sm_config();
    sm_config_set_wrap(&r, read_offset, read_offset + 10u);
    sm_config_set_in_pins(&r, data0_pin);
    sm_config_set_jmp_pin(&r, data0_pin);
    sm_config_set_in_shift(&r, false, true, 32);
    sm_config_set_out_shift(&r, false, false, 32);
    pio_sm_init(read_pio, read_sm, read_offset, &r);

    pio_sm_set_pins_with_mask64(pio, sm, 0ull, 1ull << clock_pin);
    pio_sm_set_pindirs_with_mask64(pio, sm, 1ull << clock_pin,
                                   (1ull << clock_pin) | (1ull << command_pin) |
                                       (0xfull << data0_pin));
    unsigned long actual = 0;
    mm_pico_sdio_set_divider(s, mm_pico_sdio_divider(MM_PICO_SDIO_IDENTIFY_HZ, &actual));
    pio_sm_set_enabled(pio, sm, true);
    pio_sm_set_enabled(pio, (uint)write_sm, true);

    s->configured = 1;
    mm_pico_sdio_claim_pins(s, MM_PICO_OWNER_SDIO);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_sdio_clock(unsigned int instance, unsigned long hz, unsigned long* actual_hz) {
    const int ready = mm_pico_sdio_ready(instance);
    if (ready != MM_PICO_MCU_OK) return ready;
    if (hz == 0 || actual_hz == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US)) return MM_PICO_MCU_TIMEOUT;
    mm_pico_sdio_set_divider(s, mm_pico_sdio_divider(hz, actual_hz));
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_sdio_idle_clocks(unsigned int instance, unsigned int count) {
    const int ready = mm_pico_sdio_ready(instance);
    if (ready != MM_PICO_MCU_OK) return ready;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    while (count != 0) {
        const unsigned int run = count > 65536u ? 65536u : count;
        mm_pico_sdio_queue_clocks(s, run);
        count -= run;
    }
    if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_DATA_US)) {
        mm_pico_sdio_reset(s, s->command_sm, 0);
        return MM_PICO_MCU_TIMEOUT;
    }
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_sdio_command(unsigned int instance, unsigned int index, uint32_t argument,
                             int response, uint32_t* words, size_t count) {
    const int ready = mm_pico_sdio_ready(instance);
    if (ready != MM_PICO_MCU_OK) return ready;
    const int is_long = response == MM_PICO_MCU_SDIO_LONG;
    const size_t needed = response == MM_PICO_MCU_SDIO_NONE ? 0u : is_long ? 4u : 1u;
    if (index > 63u || count < needed || (needed != 0 && words == NULL))
        return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    const unsigned int bits = needed == 0 ? 0u : is_long ? 136u : 48u;
    mm_pico_sdio_queue_command(s, index, argument, bits, 8u);
    if (bits == 0) {
        if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US)) {
            mm_pico_sdio_reset(s, s->command_sm, 0);
            return MM_PICO_MCU_TIMEOUT;
        }
        return MM_PICO_MCU_OK;
    }
    uint32_t raw[5];
    unsigned int taken = 0;
    const unsigned int expected = is_long ? 5u : 2u;
    const uint64_t start = time_us_64();
    while (!mm_pico_sdio_take(s, raw, expected, &taken)) {
        if (mm_pico_sdio_expired(start, MM_PICO_SDIO_RESPONSE_US)) {
            mm_pico_sdio_fail(MM_PICO_MCU_TIMEOUT, MM_PICO_SDIO_FAIL_RESPONSE_TIMEOUT, index, 0,
                              0);
            mm_pico_sdio_reset(s, s->command_sm, 0);
            return MM_PICO_MCU_TIMEOUT;
        }
    }
    if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US)) {
        mm_pico_sdio_reset(s, s->command_sm, 0);
        return mm_pico_sdio_fail(MM_PICO_MCU_TIMEOUT, MM_PICO_SDIO_FAIL_RESPONSE_TIMEOUT, index,
                                 0, 0);
    }
    int status;
    if (is_long) {
        status = mm_pico_sdio_long(raw, words);
    } else {
        status = mm_pico_sdio_short(raw, index, response != MM_PICO_MCU_SDIO_SHORT_NO_CRC, words);
    }
    if (status != MM_PICO_MCU_OK)
        return mm_pico_sdio_fail(status, MM_PICO_SDIO_FAIL_RESPONSE_FRAME, index, raw[0], 0);
    if (response == MM_PICO_MCU_SDIO_SHORT_BUSY) return mm_pico_sdio_wait_busy(s);
    return MM_PICO_MCU_OK;
}

// The card status bits that say a command failed: OUT_OF_RANGE through
// ERROR, CARD_IS_LOCKED excepted, and AKE_SEQ_ERROR.
#define MM_PICO_SDIO_STATUS_ERRORS 0xfdf80008u

int mm_pico_mcu_sdio_read(unsigned int instance, unsigned int index, uint32_t argument,
                          uint32_t* response, void* data, size_t size, unsigned int block_size) {
    const int ready = mm_pico_sdio_ready(instance);
    if (ready != MM_PICO_MCU_OK) return ready;
    if (block_size != MM_PICO_SDIO_BLOCK || size == 0 || size % MM_PICO_SDIO_BLOCK != 0 ||
        size / MM_PICO_SDIO_BLOCK > MM_PICO_SDIO_MOST_READ_BLOCKS || data == NULL ||
        ((uintptr_t)data & 3u) != 0 || response == NULL || index > 63u)
        return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    const unsigned int blocks = (unsigned int)(size / MM_PICO_SDIO_BLOCK);
    if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US)) {
        mm_pico_sdio_reset(s, s->command_sm, 0);
        return MM_PICO_MCU_TIMEOUT;
    }

    // Ready for data before the command: a card may start a block while its
    // response is still on the command line.
    mm_pico_sdio_stop_read(s);
    // Each block: 128 data words into the caller's buffer, byte-swapped so
    // the first nibble's byte lands first, then 2 CRC words into the table;
    // each channel triggers the other when it finishes, and the data
    // channel's write address carries on from where the last block ended.
    const volatile void* fifo = &s->read_pio->rxf[s->read_sm];
    const uint dreq = pio_get_dreq(s->read_pio, s->read_sm, false);
    dma_channel_config d = dma_channel_get_default_config((uint)s->dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_bswap(&d, true);
    channel_config_set_dreq(&d, dreq);
    channel_config_set_chain_to(&d, (uint)s->crc_dma);
    dma_channel_config c = dma_channel_get_default_config((uint)s->crc_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, dreq);
    channel_config_set_chain_to(&c, (uint)s->dma);
    dma_channel_configure((uint)s->crc_dma, &c, mm_pico_sdio_read_crcs, fifo, 2u, false);
    dma_channel_configure((uint)s->dma, &d, data, fifo, MM_PICO_SDIO_DATA_WORDS, true);
    volatile uint32_t* const crc_written = &dma_channel_hw_addr((uint)s->crc_dma)->write_addr;
    const uint32_t crc_end = (uint32_t)(uintptr_t)&mm_pico_sdio_read_crcs[blocks * 2u];
    pio_sm_put_blocking(s->read_pio, s->read_sm, MM_PICO_SDIO_READ_WORDS * 8u - 1u);
    pio_sm_set_enabled(s->read_pio, s->read_sm, true);

    // The command, then clocks in runs until the data is in.
    mm_pico_sdio_queue_command(s, index, argument, 48u, 65536u);
    uint32_t raw[2];
    unsigned int taken = 0;
    int answered = 0;
    const uint64_t start = time_us_64();
    int status = MM_PICO_MCU_OK;
    unsigned int reason = MM_PICO_SDIO_FAIL_NONE;
    uint64_t answered_at = 0;
    while (*crc_written != crc_end) {
        if (!answered && mm_pico_sdio_take(s, raw, 2u, &taken)) {
            answered = 1;
            answered_at = time_us_64();
            status = mm_pico_sdio_short(raw, index, 1, response);
            reason = MM_PICO_SDIO_FAIL_RESPONSE_FRAME;
            if (status == MM_PICO_MCU_OK && (*response & MM_PICO_SDIO_STATUS_ERRORS) != 0) {
                status = MM_PICO_MCU_TRANSPORT_ERROR;
                reason = MM_PICO_SDIO_FAIL_STATUS_BITS;
            }
            if (status != MM_PICO_MCU_OK) break;
        }
        if (!answered && mm_pico_sdio_expired(start, MM_PICO_SDIO_RESPONSE_US)) {
            status = MM_PICO_MCU_TIMEOUT;
            reason = MM_PICO_SDIO_FAIL_RESPONSE_TIMEOUT;
            break;
        }
        if (mm_pico_sdio_expired(start, MM_PICO_SDIO_DATA_US)) {
            status = MM_PICO_MCU_TIMEOUT;
            reason = MM_PICO_SDIO_FAIL_DATA_TIMEOUT;
            break;
        }
        if (pio_sm_get_tx_fifo_level(s->pio, s->command_sm) <= 1u)
            mm_pico_sdio_queue_clocks(s, 65536u);
    }
    if (status == MM_PICO_MCU_OK && !answered) {
        // The data outran the response's words in the FIFO; they are there.
        while (!mm_pico_sdio_take(s, raw, 2u, &taken)) {
            if (mm_pico_sdio_expired(start, MM_PICO_SDIO_RESPONSE_US)) break;
        }
        status = taken == 2u ? mm_pico_sdio_short(raw, index, 1, response) : MM_PICO_MCU_TIMEOUT;
        reason = taken == 2u ? MM_PICO_SDIO_FAIL_RESPONSE_FRAME
                             : MM_PICO_SDIO_FAIL_RESPONSE_TIMEOUT;
    }
    mm_pico_sdio_data_us = answered_at != 0 ? (unsigned int)(time_us_64() - answered_at) : 0u;
    const unsigned int received =
        (unsigned int)((dma_channel_hw_addr((uint)s->dma)->write_addr - (uint32_t)(uintptr_t)data) /
                       4u);
    if (status != MM_PICO_MCU_OK)
        mm_pico_sdio_fail(status, reason, index,
                          reason == MM_PICO_SDIO_FAIL_STATUS_BITS ? *response
                          : taken == 2u                         ? raw[0]
                                                                : 0u,
                          received);
    mm_pico_sdio_reset(s, s->command_sm, 0);
    mm_pico_sdio_stop_read(s);
    // The loop ends on the last CRC nibble, before the card has sent its end
    // bit; clock it out, and the eight cycles a card is owed after a
    // transfer, before anything else is sent.
    mm_pico_sdio_queue_clocks(s, 16u);
    if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US))
        mm_pico_sdio_reset(s, s->command_sm, 0);
    if (status != MM_PICO_MCU_OK) return status;

    const unsigned char* bytes = (const unsigned char*)data;
    for (unsigned int b = 0; b < blocks; ++b) {
        const uint64_t sent = ((uint64_t)mm_pico_sdio_read_crcs[2u * b] << 32) |
                              mm_pico_sdio_read_crcs[2u * b + 1u];
        if (mm_pico_sdio_crc(bytes + (size_t)b * MM_PICO_SDIO_BLOCK, MM_PICO_SDIO_BLOCK) != sent)
            return mm_pico_sdio_fail(MM_PICO_MCU_TRANSPORT_ERROR, MM_PICO_SDIO_FAIL_DATA_CRC,
                                     index, *response, b);
    }
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_sdio_write(unsigned int instance, unsigned int index, uint32_t argument,
                           uint32_t* response, const void* data, size_t size,
                           unsigned int block_size) {
    const int ready = mm_pico_sdio_ready(instance);
    if (ready != MM_PICO_MCU_OK) return ready;
    if (block_size != MM_PICO_SDIO_BLOCK || size == 0 || size % MM_PICO_SDIO_BLOCK != 0 ||
        data == NULL || response == NULL || index > 63u)
        return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    uint32_t words[1];
    int status = mm_pico_mcu_sdio_command(instance, index, argument, MM_PICO_MCU_SDIO_SHORT,
                                          words, 1);
    if (status != MM_PICO_MCU_OK) return status;
    *response = words[0];
    if ((*response & MM_PICO_SDIO_STATUS_ERRORS) != 0) return MM_PICO_MCU_TRANSPORT_ERROR;

    const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + s->write_sm);
    const unsigned char* in = (const unsigned char*)data;
    for (size_t at = 0; at < size; at += MM_PICO_SDIO_BLOCK) {
        const unsigned char* bytes = in + at;
        const uint64_t crc = mm_pico_sdio_crc(bytes, MM_PICO_SDIO_BLOCK);
        // A start nibble, the data a nibble late, the CRC nibbles, an end
        // nibble: 1042 nibbles, most significant first in each word, each
        // word made as it is fed.
        s->pio->fdebug = stall;
        pio_sm_put_blocking(s->pio, s->write_sm, MM_PICO_SDIO_WRITE_NIBBLES - 1u);
        uint32_t previous = 0;
        for (unsigned int w = 0; w < MM_PICO_SDIO_DATA_WORDS; ++w) {
            const uint32_t word = ((uint32_t)bytes[4u * w] << 24) |
                                  ((uint32_t)bytes[4u * w + 1u] << 16) |
                                  ((uint32_t)bytes[4u * w + 2u] << 8) | bytes[4u * w + 3u];
            pio_sm_put_blocking(s->pio, s->write_sm, (previous << 28) | (word >> 4));
            previous = word;
        }
        pio_sm_put_blocking(s->pio, s->write_sm, (previous << 28) | (uint32_t)(crc >> 36));
        pio_sm_put_blocking(s->pio, s->write_sm, (uint32_t)(crc >> 4));
        pio_sm_put_blocking(s->pio, s->write_sm, ((uint32_t)(crc & 0xfu) << 28) | 0x0f000000u);

        const uint64_t start = time_us_64();
        while (pio_sm_is_rx_fifo_empty(s->pio, s->write_sm)) {
            if (mm_pico_sdio_expired(start, MM_PICO_SDIO_RESPONSE_US)) {
                mm_pico_sdio_reset(s, s->write_sm, MM_PICO_SDIO_WRITE_BASE);
                return MM_PICO_MCU_TIMEOUT;
            }
        }
        const uint32_t token = pio_sm_get(s->pio, s->write_sm) & 0xfu;
        s->pio->fdebug = stall;
        while ((s->pio->fdebug & stall) == 0) {    // busy, then back at its PULL
            if (mm_pico_sdio_expired(start, MM_PICO_SDIO_BUSY_US)) {
                mm_pico_sdio_reset(s, s->write_sm, MM_PICO_SDIO_WRITE_BASE);
                return MM_PICO_MCU_TIMEOUT;
            }
        }
        if (token != 0x5u) return MM_PICO_MCU_TRANSPORT_ERROR;    // 010: accepted
        // The state machine looks at D0 one clock after the token's end bit,
        // which can be before the card asserts busy; eight more clocks and a
        // look from here make sure it is over before the next block.
        mm_pico_sdio_queue_clocks(s, 8);
        if (!mm_pico_sdio_command_idle(s, MM_PICO_SDIO_RESPONSE_US)) {
            mm_pico_sdio_reset(s, s->command_sm, 0);
            return MM_PICO_MCU_TIMEOUT;
        }
        status = mm_pico_sdio_wait_busy(s);
        if (status != MM_PICO_MCU_OK) return status;
    }
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_sdio_release(unsigned int instance) {
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;
    mm_pico_sdio_t* s = &mm_pico_sdio;
    if (!s->configured) return MM_PICO_MCU_OK;
    mm_pico_sdio_stop_read(s);
    pio_sm_set_enabled(s->pio, s->command_sm, false);
    pio_sm_set_enabled(s->pio, s->write_sm, false);
    dma_channel_unclaim((uint)s->dma);
    dma_channel_unclaim((uint)s->crc_dma);
    pio_remove_program_and_unclaim_sm(&s->read_program, s->read_pio, s->read_sm, s->read_offset);
    pio_sm_unclaim(s->pio, s->write_sm);
    const uint64_t mask = (1ull << s->clock_pin) | (1ull << s->command_pin) | (0xfull << s->data0_pin);
    pio_set_input_sync_bypass_with_mask64(s->pio, 0, mask);
    pio_set_input_sync_bypass_with_mask64(s->read_pio, 0, mask);
    gpio_deinit(s->clock_pin);
    gpio_deinit(s->command_pin);
    for (unsigned int i = 0; i < 4u; ++i) gpio_deinit(s->data0_pin + i);
    mm_pico_sdio_claim_pins(s, MM_PICO_OWNER_NONE);
    s->configured = 0;
    return MM_PICO_MCU_OK;
}

// The flash region: the top MM_BOARD_FLASH_REGION_BYTES of the flash the
// build assumes, PICO_FLASH_SIZE_BYTES. Reads come straight from the
// execute-in-place window; programs and erases go through flash_safe_execute,
// which keeps the other core and this core's interrupts off the flash while
// it cannot be read. Programs are bounced through a page in RAM, because the
// data being programmed must not itself live in the flash being programmed.

#define MM_PICO_FLASH_REGION_START \
    ((unsigned long long)PICO_FLASH_SIZE_BYTES - (unsigned long long)MM_BOARD_FLASH_REGION_BYTES)
#define MM_PICO_FLASH_SAFE_TIMEOUT_MS 1000u

extern char __flash_binary_end;

static uint8_t mm_pico_flash_page[FLASH_PAGE_SIZE];

typedef struct {
    uint32_t flash_offset;
    size_t size;
} mm_pico_flash_operation_t;

// Usable only when the board gives it bytes and the image ends below it. The
// bridge refuses an overlapping image at build time; this is the same check
// at run time, for an image linked some other way.
static int mm_pico_flash_region_usable(void) {
    if (MM_BOARD_FLASH_REGION_BYTES == 0) return 0;
    if ((unsigned long long)MM_BOARD_FLASH_REGION_BYTES >= (unsigned long long)PICO_FLASH_SIZE_BYTES)
        return 0;
    return (uintptr_t)&__flash_binary_end <= (uintptr_t)XIP_BASE + MM_PICO_FLASH_REGION_START;
}

static int mm_pico_flash_in_region(unsigned long long offset, unsigned long long size) {
    return size <= (unsigned long long)MM_BOARD_FLASH_REGION_BYTES &&
           offset <= (unsigned long long)MM_BOARD_FLASH_REGION_BYTES - size;
}

static int mm_pico_flash_from_safe(int result) {
    if (result == PICO_OK) return MM_PICO_MCU_OK;
    if (result == PICO_ERROR_TIMEOUT) return MM_PICO_MCU_TIMEOUT;
    return MM_PICO_MCU_BUSY;
}

static void mm_pico_flash_program_page(void* parameter) {
    const mm_pico_flash_operation_t* operation = (const mm_pico_flash_operation_t*)parameter;
    flash_range_program(operation->flash_offset, mm_pico_flash_page, operation->size);
}

static void mm_pico_flash_erase_sector(void* parameter) {
    const mm_pico_flash_operation_t* operation = (const mm_pico_flash_operation_t*)parameter;
    flash_range_erase(operation->flash_offset, operation->size);
}

int mm_pico_mcu_flash_region_geometry(unsigned long long* size, unsigned int* read_size,
                                      unsigned int* program_size, unsigned int* erase_size) {
    if (size == NULL || read_size == NULL || program_size == NULL || erase_size == NULL)
        return MM_PICO_MCU_BAD_ARGUMENT;
    if (!mm_pico_flash_region_usable()) return MM_PICO_MCU_UNSUPPORTED;
    *size = (unsigned long long)MM_BOARD_FLASH_REGION_BYTES;
    *read_size = 1u;
    *program_size = FLASH_PAGE_SIZE;
    *erase_size = FLASH_SECTOR_SIZE;
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_flash_region_read(unsigned long long offset, void* data, size_t size) {
    if (!mm_pico_flash_region_usable()) return MM_PICO_MCU_UNSUPPORTED;
    if (data == NULL || size == 0 || !mm_pico_flash_in_region(offset, size))
        return MM_PICO_MCU_BAD_ARGUMENT;
    memcpy(data, (const void*)(XIP_BASE + (uintptr_t)(MM_PICO_FLASH_REGION_START + offset)),
           size);
    return MM_PICO_MCU_OK;
}

// One page per safe section, so interrupts are held off for one page's
// program time at a time rather than for the whole transfer.
int mm_pico_mcu_flash_region_program(unsigned long long offset, const void* data, size_t size) {
    if (!mm_pico_flash_region_usable()) return MM_PICO_MCU_UNSUPPORTED;
    if (data == NULL || size == 0 || offset % FLASH_PAGE_SIZE != 0 ||
        size % FLASH_PAGE_SIZE != 0 || !mm_pico_flash_in_region(offset, size))
        return MM_PICO_MCU_BAD_ARGUMENT;
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t done = 0; done < size; done += FLASH_PAGE_SIZE) {
        memcpy(mm_pico_flash_page, bytes + done, FLASH_PAGE_SIZE);
        mm_pico_flash_operation_t operation = {
            (uint32_t)(MM_PICO_FLASH_REGION_START + offset + done), FLASH_PAGE_SIZE};
        const int status = mm_pico_flash_from_safe(flash_safe_execute(
            mm_pico_flash_program_page, &operation, MM_PICO_FLASH_SAFE_TIMEOUT_MS));
        if (status != MM_PICO_MCU_OK) return status;
    }
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_flash_region_erase(unsigned long long offset, unsigned long long size) {
    if (!mm_pico_flash_region_usable()) return MM_PICO_MCU_UNSUPPORTED;
    if (size == 0 || offset % FLASH_SECTOR_SIZE != 0 || size % FLASH_SECTOR_SIZE != 0 ||
        !mm_pico_flash_in_region(offset, size))
        return MM_PICO_MCU_BAD_ARGUMENT;
    for (unsigned long long done = 0; done < size; done += FLASH_SECTOR_SIZE) {
        mm_pico_flash_operation_t operation = {
            (uint32_t)(MM_PICO_FLASH_REGION_START + offset + done), FLASH_SECTOR_SIZE};
        const int status = mm_pico_flash_from_safe(flash_safe_execute(
            mm_pico_flash_erase_sector, &operation, MM_PICO_FLASH_SAFE_TIMEOUT_MS));
        if (status != MM_PICO_MCU_OK) return status;
    }
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_interrupts_disable(unsigned int* saved) {
    if (saved == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *saved = (unsigned int)save_and_disable_interrupts();
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_interrupts_enable(unsigned int saved) {
    restore_interrupts((uint32_t)saved);
    return MM_PICO_MCU_OK;
}

void mm_pico_mcu_default_spi(unsigned int* instance, unsigned int* clock_pin,
    unsigned int* transmit_pin, unsigned int* receive_pin, int* has_receive,
    unsigned int* chip_select_pin) {
#ifdef WAVESHARE_PICO_CAM_A
    *instance = WAVESHARE_LCD_SPI;
    *clock_pin = WAVESHARE_LCD_SCLK_PIN;
    *transmit_pin = WAVESHARE_LCD_TX_PIN;
    *receive_pin = 0;
    *has_receive = 0;
    *chip_select_pin = WAVESHARE_LCD_CS_PIN;
#else
    *instance = 0; *clock_pin = 18; *transmit_pin = 19;
    *receive_pin = 16; *has_receive = 1; *chip_select_pin = 15;
#endif
}

void mm_pico_mcu_default_i2c(unsigned int* instance, unsigned int* data_pin,
    unsigned int* clock_pin) {
#ifdef WAVESHARE_PICO_CAM_A
    *instance = PICO_DEFAULT_I2C;
    *data_pin = PICO_DEFAULT_I2C_SDA_PIN;
    *clock_pin = PICO_DEFAULT_I2C_SCL_PIN;
#else
    *instance = 0; *data_pin = 4; *clock_pin = 5;
#endif
}

const char* mm_pico_mcu_board_name(void) {
    return MM_SELECTED_BOARD;
}

unsigned int mm_pico_mcu_gpio_count(void) {
    return (unsigned int)NUM_BANK0_GPIOS;
}

// A board whose vendor ancestor's LED pin is something else on it says so
// through MM_BOARD_HAS_LED, from the bridge's board table.
int mm_pico_mcu_has_led(void) {
#if defined(PICO_DEFAULT_LED_PIN) && MM_BOARD_HAS_LED
    return 1;
#else
    return 0;
#endif
}

// A board with a second I2C wiring says so through MM_BOARD_HAS_SECOND_I2C,
// from the bridge's board table.
int mm_pico_mcu_has_second_i2c(void) {
#if MM_BOARD_HAS_SECOND_I2C
    return 1;
#else
    return 0;
#endif
}

unsigned int mm_pico_mcu_led_gpio(void) {
#if defined(PICO_DEFAULT_LED_PIN) && MM_BOARD_HAS_LED
    return (unsigned int)PICO_DEFAULT_LED_PIN;
#else
    return 0;
#endif
}

int mm_pico_mcu_led_active_high(void) {
#if defined(PICO_DEFAULT_LED_PIN_INVERTED) && PICO_DEFAULT_LED_PIN_INVERTED
    return 0;
#else
    return 1;
#endif
}

// Block storage over TinyUSB's mass-storage host. One device, logical unit
// zero. Nothing runs the host stack between calls: each call runs it on the
// caller's thread, poll for a moment and a transfer until it completes, so a
// drive is noticed when the program asks.
#if MM_BOARD_HAS_USB_HOST
static uint8_t mm_pico_storage_address;
static volatile int mm_pico_storage_done;
static volatile int mm_pico_storage_passed;

void tuh_msc_mount_cb(uint8_t dev_addr) { mm_pico_storage_address = dev_addr; }

void tuh_msc_umount_cb(uint8_t dev_addr) {
    if (mm_pico_storage_address == dev_addr) mm_pico_storage_address = 0;
}

static bool mm_pico_storage_complete(uint8_t dev_addr, tuh_msc_complete_data_t const* data) {
    (void)dev_addr;
    mm_pico_storage_passed = data->csw->status == MSC_CSW_STATUS_PASSED;
    mm_pico_storage_done = 1;
    return true;
}

static int mm_pico_storage_ready(void) {
    return mm_pico_storage_address != 0 && tuh_msc_mounted(mm_pico_storage_address);
}

// Runs the host stack until the transfer in flight completes, the device goes,
// or the deadline passes.
static int mm_pico_storage_wait(void) {
    const absolute_time_t deadline = make_timeout_time_ms(5000);
    while (!mm_pico_storage_done) {
        tuh_task();
        if (!mm_pico_storage_ready()) return MM_PICO_MCU_TRANSPORT_ERROR;
        if (time_reached(deadline)) return MM_PICO_MCU_TIMEOUT;
    }
    return mm_pico_storage_passed ? MM_PICO_MCU_OK : MM_PICO_MCU_TRANSPORT_ERROR;
}

static int mm_pico_storage_check(unsigned long long block, unsigned long size,
                                 unsigned int* per_block) {
    if (!mm_pico_storage_ready()) return MM_PICO_MCU_TRANSPORT_ERROR;
    const uint32_t block_size = tuh_msc_get_block_size(mm_pico_storage_address, 0);
    const uint32_t block_count = tuh_msc_get_block_count(mm_pico_storage_address, 0);
    if (block_size == 0 || size == 0 || size % block_size != 0)
        return MM_PICO_MCU_BAD_ARGUMENT;
    if (block > block_count || size / block_size > block_count - block)
        return MM_PICO_MCU_BAD_ARGUMENT;
    *per_block = block_size;
    return MM_PICO_MCU_OK;
}

// Transfers go in pieces of at most this many blocks, so one buffer never
// holds the host stack for long.
enum { MM_PICO_STORAGE_CHUNK_BLOCKS = 32 };
#endif

int mm_pico_mcu_has_storage(void) {
#if MM_BOARD_HAS_USB_HOST
    return 1;
#else
    return 0;
#endif
}

int mm_pico_mcu_storage_poll(int* present) {
#if MM_BOARD_HAS_USB_HOST
    if (present == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    mm_pico_usb_start();
    const absolute_time_t until = make_timeout_time_ms(2);
    do {
        tuh_task();
    } while (!time_reached(until));
    *present = mm_pico_storage_ready();
    return MM_PICO_MCU_OK;
#else
    (void)present;
    return MM_PICO_MCU_UNSUPPORTED;
#endif
}

int mm_pico_mcu_storage_geometry(unsigned long long* block_count, unsigned int* block_size) {
#if MM_BOARD_HAS_USB_HOST
    if (block_count == NULL || block_size == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    if (!mm_pico_storage_ready()) return MM_PICO_MCU_TRANSPORT_ERROR;
    *block_count = tuh_msc_get_block_count(mm_pico_storage_address, 0);
    *block_size = tuh_msc_get_block_size(mm_pico_storage_address, 0);
    return MM_PICO_MCU_OK;
#else
    (void)block_count;
    (void)block_size;
    return MM_PICO_MCU_UNSUPPORTED;
#endif
}

int mm_pico_mcu_storage_read(unsigned long long block, void* data, unsigned long size) {
#if MM_BOARD_HAS_USB_HOST
    if (data == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    unsigned int block_size = 0;
    int status = mm_pico_storage_check(block, size, &block_size);
    if (status != MM_PICO_MCU_OK) return status;
    uint8_t* out = (uint8_t*)data;
    unsigned long remaining = size / block_size;
    while (remaining != 0) {
        const uint16_t count = (uint16_t)(remaining < MM_PICO_STORAGE_CHUNK_BLOCKS
                                              ? remaining
                                              : MM_PICO_STORAGE_CHUNK_BLOCKS);
        mm_pico_storage_done = 0;
        if (!tuh_msc_read10(mm_pico_storage_address, 0, out, (uint32_t)block, count,
                            mm_pico_storage_complete, 0))
            return MM_PICO_MCU_BUSY;
        status = mm_pico_storage_wait();
        if (status != MM_PICO_MCU_OK) return status;
        out += (unsigned long)count * block_size;
        block += count;
        remaining -= count;
    }
    return MM_PICO_MCU_OK;
#else
    (void)block;
    (void)data;
    (void)size;
    return MM_PICO_MCU_UNSUPPORTED;
#endif
}

int mm_pico_mcu_storage_write(unsigned long long block, const void* data, unsigned long size) {
#if MM_BOARD_HAS_USB_HOST
    if (data == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    unsigned int block_size = 0;
    int status = mm_pico_storage_check(block, size, &block_size);
    if (status != MM_PICO_MCU_OK) return status;
    const uint8_t* in = (const uint8_t*)data;
    unsigned long remaining = size / block_size;
    while (remaining != 0) {
        const uint16_t count = (uint16_t)(remaining < MM_PICO_STORAGE_CHUNK_BLOCKS
                                              ? remaining
                                              : MM_PICO_STORAGE_CHUNK_BLOCKS);
        mm_pico_storage_done = 0;
        if (!tuh_msc_write10(mm_pico_storage_address, 0, in, (uint32_t)block, count,
                             mm_pico_storage_complete, 0))
            return MM_PICO_MCU_BUSY;
        status = mm_pico_storage_wait();
        if (status != MM_PICO_MCU_OK) return status;
        in += (unsigned long)count * block_size;
        block += count;
        remaining -= count;
    }
    return MM_PICO_MCU_OK;
#else
    (void)block;
    (void)data;
    (void)size;
    return MM_PICO_MCU_UNSUPPORTED;
#endif
}

#include "adapter_usb_device.c"
#include "adapter_usb_host.c"

// TinyUSB 0.18.0, in the pinned Pico SDK 2.3.1, panics with "Can't continue
// xfer on inactive ep" when the controller reports a finished buffer for an
// endpoint whose transfer has already ended. A host that abandons a
// double-buffered control IN transfer (a configuration descriptor) while it
// re-enumerates the board leaves exactly that notification behind; it was
// caught on endpoint 0x80. Upstream TinyUSB now ignores such a notification,
// returning false for an idle endpoint. The bridge links with
// --wrap=hw_endpoint_xfer_continue so that this wrapper does the same until
// the pinned SDK carries that fix; the SDK checkout itself stays untouched.
#include "tusb.h"
#include "portable/raspberrypi/rp2040/rp2040_usb.h"

bool __real_hw_endpoint_xfer_continue(struct hw_endpoint* ep);
bool __wrap_hw_endpoint_xfer_continue(struct hw_endpoint* ep);

bool __not_in_flash_func(__wrap_hw_endpoint_xfer_continue)(
    struct hw_endpoint* ep) {
    if (!ep->active) return false;
    return __real_hw_endpoint_xfer_continue(ep);
}

// Board camera implementation stays in this adapter, the SDK header boundary.
#if MM_PICO_CAM_CAMERA
#include "hardware/vreg.h"
#include "../../../../../boards/pico_cam_a/camera/capture.inc.c"
#endif
