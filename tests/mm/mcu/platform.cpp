// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A stand-in platform for mm.mcu. It defines the same extern "C" ABI a board or
// a vendor adapter defines, which is what makes it a demonstration of the supply
// mechanism rather than a mock of it: nothing here is test scaffolding that a
// real platform would not also have to provide.

#include "../../../modules/mm/mcu/mcu-cxx.h"

namespace {

constexpr unsigned int pin_count = 32;

bool configured[pin_count] = {};
bool output[pin_count] = {};
bool level[pin_count] = {};

}  // namespace

extern "C" {

// Observation and control for the test. A real platform has neither, and the
// interface cannot see them: they are not part of the ABI it declares.
int mm_test_forced_status = -1;
unsigned long mm_test_ticks = 0;
unsigned int mm_test_uart_instance = 0;
const char* mm_test_uart_text = nullptr;

void mm_test_reset() {
    for (unsigned int pin = 0; pin < pin_count; ++pin) {
        configured[pin] = false;
        output[pin] = false;
        level[pin] = false;
    }
    mm_test_forced_status = -1;
    mm_test_ticks = 0;
    mm_test_uart_instance = 0;
    mm_test_uart_text = nullptr;
}

void mm_test_set_level(unsigned int pin, int high) {
    if (pin < pin_count) level[pin] = high != 0;
}

int mm_mcu_gpio_configure(unsigned int pin, int direction, int pull) {
    if (mm_test_forced_status >= 0) return mm_test_forced_status;
    if (pin >= pin_count) return MM_MCU_BAD_ARGUMENT;
    if (direction != 0 && direction != 1) return MM_MCU_BAD_ARGUMENT;
    if (pull < 0 || pull > 2) return MM_MCU_BAD_ARGUMENT;
    configured[pin] = true;
    output[pin] = direction == 1;
    return MM_MCU_OK;
}

int mm_mcu_gpio_write(unsigned int pin, int high) {
    if (mm_test_forced_status >= 0) return mm_test_forced_status;
    if (pin >= pin_count) return MM_MCU_BAD_ARGUMENT;
    if (!configured[pin]) return MM_MCU_BAD_ARGUMENT;
    if (!output[pin]) return MM_MCU_UNSUPPORTED;
    level[pin] = high != 0;
    return MM_MCU_OK;
}

int mm_mcu_gpio_read(unsigned int pin, int* high) {
    if (mm_test_forced_status >= 0) return mm_test_forced_status;
    if (pin >= pin_count || high == nullptr) return MM_MCU_BAD_ARGUMENT;
    if (!configured[pin]) return MM_MCU_BAD_ARGUMENT;
    *high = level[pin] ? 1 : 0;
    return MM_MCU_OK;
}

int mm_mcu_uart_write(unsigned int instance, const char* text) {
    if (mm_test_forced_status >= 0) return mm_test_forced_status;
    if (text == nullptr) return MM_MCU_BAD_ARGUMENT;
    if (instance != 0) return MM_MCU_UNSUPPORTED;
    mm_test_uart_instance = instance;
    mm_test_uart_text = text;
    return MM_MCU_OK;
}

int mm_mcu_delay_ms(unsigned long milliseconds) {
    if (mm_test_forced_status >= 0) return mm_test_forced_status;
    mm_test_ticks += milliseconds;
    return MM_MCU_OK;
}

int mm_mcu_ticks_ms(unsigned long* ticks) {
    if (mm_test_forced_status >= 0) return mm_test_forced_status;
    if (ticks == nullptr) return MM_MCU_BAD_ARGUMENT;
    *ticks = mm_test_ticks;
    return MM_MCU_OK;
}

}  // extern "C"
