#include "pico-c.h"
// The private ABI beneath platform.pico.mcu, declared beside that module
// rather than here: this file implements it, and the module calls it.
#include "../mcu/mcu-c.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <limits.h>
#include <stdio.h>

// The vendor surface: ext.pico, for code that wants Pico SDK specifically.

void mm_pico_initialize(void) {
    stdio_init_all();
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

// The platform surface: the private ABI beneath platform.pico.mcu. Both live in
// one adapter because this is the only translation unit permitted to include SDK
// headers, and a second one would be a second place for that permission to be
// reviewed. These names are this bridge's own; the portable contract is mm.mcu,
// and it is pure C++.

static int mm_pico_pin_valid(unsigned int pin) {
    return pin < (unsigned int)NUM_BANK0_GPIOS;
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
    if (direction != MM_PICO_MCU_DIRECTION_IN && direction != MM_PICO_MCU_DIRECTION_OUT)
        return MM_PICO_MCU_BAD_ARGUMENT;

    gpio_init(pin);
    gpio_set_dir(pin, direction == MM_PICO_MCU_DIRECTION_OUT);

    switch (pull) {
        case MM_PICO_MCU_PULL_NONE: gpio_disable_pulls(pin); break;
        case MM_PICO_MCU_PULL_UP: gpio_pull_up(pin); break;
        case MM_PICO_MCU_PULL_DOWN: gpio_pull_down(pin); break;
        default: return MM_PICO_MCU_BAD_ARGUMENT;
    }
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_write(unsigned int pin, int high) {
    if (!mm_pico_pin_valid(pin)) return MM_PICO_MCU_BAD_ARGUMENT;
    gpio_put(pin, high != 0);
    return MM_PICO_MCU_OK;
}

int mm_pico_mcu_gpio_read(unsigned int pin, int* high) {
    if (!mm_pico_pin_valid(pin) || high == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *high = gpio_get(pin) ? 1 : 0;
    return MM_PICO_MCU_OK;
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

int mm_pico_mcu_ticks_ms(unsigned long* ticks) {
    if (ticks == NULL) return MM_PICO_MCU_BAD_ARGUMENT;
    *ticks = (unsigned long)to_ms_since_boot(get_absolute_time());
    return MM_PICO_MCU_OK;
}

const char* mm_pico_mcu_board_name(void) {
    return PICO_BOARD;
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
