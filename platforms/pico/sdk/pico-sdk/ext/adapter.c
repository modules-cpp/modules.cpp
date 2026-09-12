#include "pico-c.h"
#include "mcu-c.h"
#include "pico/stdlib.h"
#include "pico/time.h"
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

// The portable surface: the platform's half of mm.mcu. Both live in one adapter
// because this is the only translation unit permitted to include SDK headers, and
// a second one would be a second place for that permission to be reviewed.

static int mm_pico_pin_valid(unsigned int pin) {
    return pin < (unsigned int)NUM_BANK0_GPIOS;
}

int mm_mcu_gpio_configure(unsigned int pin, int direction, int pull) {
    if (!mm_pico_pin_valid(pin)) return MM_MCU_BAD_ARGUMENT;
    if (direction != MM_MCU_DIRECTION_IN && direction != MM_MCU_DIRECTION_OUT)
        return MM_MCU_BAD_ARGUMENT;

    gpio_init(pin);
    gpio_set_dir(pin, direction == MM_MCU_DIRECTION_OUT);

    switch (pull) {
        case MM_MCU_PULL_NONE: gpio_disable_pulls(pin); break;
        case MM_MCU_PULL_UP: gpio_pull_up(pin); break;
        case MM_MCU_PULL_DOWN: gpio_pull_down(pin); break;
        default: return MM_MCU_BAD_ARGUMENT;
    }
    return MM_MCU_OK;
}

int mm_mcu_gpio_write(unsigned int pin, int high) {
    if (!mm_pico_pin_valid(pin)) return MM_MCU_BAD_ARGUMENT;
    gpio_put(pin, high != 0);
    return MM_MCU_OK;
}

int mm_mcu_gpio_read(unsigned int pin, int* high) {
    if (!mm_pico_pin_valid(pin) || high == NULL) return MM_MCU_BAD_ARGUMENT;
    *high = gpio_get(pin) ? 1 : 0;
    return MM_MCU_OK;
}

// Instance zero is the SDK's configured stdio, which the bridge points at
// semihosting. A second instance would mean driving a UART the SDK has not been
// told to own, so it is Unsupported rather than silently the same one.
int mm_mcu_uart_write(unsigned int instance, const char* text) {
    if (text == NULL) return MM_MCU_BAD_ARGUMENT;
    if (instance != 0) return MM_MCU_UNSUPPORTED;
    if (puts(text) < 0) return MM_MCU_BUSY;
    return MM_MCU_OK;
}

int mm_mcu_delay_ms(unsigned long milliseconds) {
    sleep_ms(milliseconds);
    return MM_MCU_OK;
}

int mm_mcu_ticks_ms(unsigned long* ticks) {
    if (ticks == NULL) return MM_MCU_BAD_ARGUMENT;
    *ticks = (unsigned long)to_ms_since_boot(get_absolute_time());
    return MM_MCU_OK;
}
