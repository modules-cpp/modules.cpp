module;

#include "pico-cxx.h"

export module lib.pico;

export namespace lib::pico {

void initialize() {
    mm_pico_initialize();
}

void write(const char* text) {
    mm_pico_write(text);
}

void delay_ms(unsigned long milliseconds) {
    mm_pico_delay_ms(milliseconds);
}

void gpio_write(unsigned int pin, bool high) {
    mm_pico_gpio_write(pin, high ? 1 : 0);
}

}
