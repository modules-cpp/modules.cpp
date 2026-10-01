#include "pico-c.h"
// The private ABI beneath platform.pico.mcu, declared beside that module
// rather than here: this file implements it, and the module calls it.
#include "../mcu/mcu-c.h"
#include "../stdio/stdio-c.h"
#include "../usb/device/usb-device-c.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
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

#if MM_BOARD_HAS_USB_HOST
#include "tusb.h"
#include "pio_usb.h"
#endif

// The vendor surface: ext.pico, for code that wants Pico SDK specifically.

static int mm_pico_stdio_attempted;
static int mm_pico_stdio_ready;

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
    if (size == 0 || !stdio_usb_connected()) return MM_PICO_STDIO_OK;
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
    if (size == 0 || !stdio_usb_connected()) return MM_PICO_STDIO_OK;
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
    return MM_PICO_STDIO_OK;
#endif
}

int mm_pico_stdio_connected(int* connected) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (connected == NULL) return MM_PICO_STDIO_BAD_ARGUMENT;
#if MM_BOARD_USB_PORT_APPLICATION
    *connected = 1;
    return MM_PICO_STDIO_OK;
#else
    *connected = stdio_usb_connected() ? 1 : 0;
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
    MM_PICO_OWNER_I2S = 5
};
static unsigned char mm_pico_pin_owner[NUM_BANK0_GPIOS];

// A pad a peripheral holds: an analog claim, or an I2S link's.
static int mm_pico_analog_holds(unsigned int pin) {
    return mm_pico_pin_owner[pin] == MM_PICO_OWNER_ADC ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_PWM ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_I2S;
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
            mm_pico_pin_owner[pin] == MM_PICO_OWNER_I2S)
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
        mm_pico_pin_owner[pin] == MM_PICO_OWNER_I2S)
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

int mm_pico_mcu_interrupts_disable(unsigned int* saved) {
    if (saved == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *saved = (unsigned int)save_and_disable_interrupts();
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_interrupts_enable(unsigned int saved) {
    restore_interrupts((uint32_t)saved);
    return MM_PICO_MCU_OK;
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

