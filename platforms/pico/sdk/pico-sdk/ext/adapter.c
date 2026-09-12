#include "pico-c.h"
#include "pico/stdlib.h"
#include <stdio.h>

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
