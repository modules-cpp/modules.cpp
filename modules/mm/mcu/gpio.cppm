// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"

export module mm.mcu:gpio;

import :status;

export namespace mm::mcu {

enum class Direction { In, Out };
enum class Pull { None, Up, Down };

[[nodiscard]] inline Status gpio_configure(unsigned int pin, Direction direction, Pull pull) {
    return detail::status_from(
        mm_mcu_gpio_configure(pin, static_cast<int>(direction), static_cast<int>(pull)));
}

[[nodiscard]] inline Status gpio_write(unsigned int pin, bool high) {
    return detail::status_from(mm_mcu_gpio_write(pin, high ? 1 : 0));
}

// high is written only when the call answers Ok, so a caller that ignores the
// status cannot mistake an untouched variable for a reading.
[[nodiscard]] inline Status gpio_read(unsigned int pin, bool& high) {
    int raw = 0;
    const auto status = detail::status_from(mm_mcu_gpio_read(pin, &raw));
    if (status == Status::Ok) high = raw != 0;
    return status;
}

}
