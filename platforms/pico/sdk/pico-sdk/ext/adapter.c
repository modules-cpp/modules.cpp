#include "pico-c.h"
// The private ABI beneath platform.pico.mcu, declared beside that module
// rather than here: this file implements it, and the module calls it.
#include "../mcu/mcu-c.h"
#include "../stdio/stdio-c.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
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
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include <limits.h>
#include <stdio.h>

// The vendor surface: ext.pico, for code that wants Pico SDK specifically.

static int mm_pico_stdio_attempted;
static int mm_pico_stdio_ready;

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
#if LIB_PICO_STDIO_USB
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
    if (size == 0 || !stdio_usb_connected()) return MM_PICO_STDIO_OK;
    if (stdio_usb.out_chars == NULL) return MM_PICO_STDIO_UNSUPPORTED;

    stdio_usb.out_chars((const char*)data, (int)size);
    *written = size;
    return MM_PICO_STDIO_OK;
}

int mm_pico_stdio_read(unsigned char* data, size_t size, size_t* count) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (count == NULL || size > INT_MAX || (size != 0 && data == NULL))
        return MM_PICO_STDIO_BAD_ARGUMENT;

    *count = 0;
    if (size == 0 || !stdio_usb_connected()) return MM_PICO_STDIO_OK;
    if (stdio_usb.in_chars == NULL) return MM_PICO_STDIO_UNSUPPORTED;

    const int result = stdio_usb.in_chars((char*)data, (int)size);
    if (result > 0) {
        *count = (size_t)result;
        return MM_PICO_STDIO_OK;
    }
    if (result == PICO_ERROR_NO_DATA) return MM_PICO_STDIO_OK;
    return MM_PICO_STDIO_TRANSPORT_ERROR;
}

int mm_pico_stdio_flush(void) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (stdio_usb.out_flush == NULL) return MM_PICO_STDIO_UNSUPPORTED;
    stdio_usb.out_flush();
    return MM_PICO_STDIO_OK;
}

int mm_pico_stdio_connected(int* connected) {
    if (!mm_pico_stdio_ready) return MM_PICO_STDIO_NOT_INITIALIZED;
    if (connected == NULL) return MM_PICO_STDIO_BAD_ARGUMENT;
    *connected = stdio_usb_connected() ? 1 : 0;
    return MM_PICO_STDIO_OK;
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
    MM_PICO_OWNER_PWM = 4
};
static unsigned char mm_pico_pin_owner[NUM_BANK0_GPIOS];

static int mm_pico_analog_holds(unsigned int pin) {
    return mm_pico_pin_owner[pin] == MM_PICO_OWNER_ADC ||
           mm_pico_pin_owner[pin] == MM_PICO_OWNER_PWM;
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

// Portable instance zero means the selected SDK board's default hardware UART.
// Every Raspberry Pi Pico board definition supplies its UART number and TX/RX
// pins. A second portable instance would need its own board description and is
// Unsupported rather than silently aliasing the default.
int mm_pico_mcu_uart_write(unsigned int instance, const char* text) {
    static int initialized = 0;
    if (text == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    if (instance != 0) return MM_PICO_MCU_UNSUPPORTED;

    if (!initialized) {
        uart_init(uart_default, PICO_DEFAULT_UART_BAUD_RATE);
        gpio_set_function(PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(PICO_DEFAULT_UART_RX_PIN, GPIO_FUNC_UART);
        initialized = 1;
    }
    uart_puts(uart_default, text);
    return MM_PICO_MCU_OK;
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
            mm_pico_pin_owner[pin] == MM_PICO_OWNER_PWM)
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
        mm_pico_pin_owner[pin] == MM_PICO_OWNER_ADC)
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

const char* mm_pico_mcu_board_name(void) {
    return MM_SELECTED_BOARD;
}

unsigned int mm_pico_mcu_gpio_count(void) {
    return (unsigned int)NUM_BANK0_GPIOS;
}

int mm_pico_mcu_has_led(void) {
#ifdef PICO_DEFAULT_LED_PIN
    return 1;
#else
    return 0;
#endif
}

unsigned int mm_pico_mcu_led_gpio(void) {
#ifdef PICO_DEFAULT_LED_PIN
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
