// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:gpio;

import :status;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline Status gpio_configure(unsigned int pin, Direction direction, Pull pull) {
    return platform().gpio_configure(pin, direction, pull);
}

[[nodiscard]] inline Status gpio_write(unsigned int pin, bool high) {
    return platform().gpio_write(pin, high);
}

// high is written only when the call answers Ok, which is the platform's
// obligation and is exercised by the test suite for every implementation.
[[nodiscard]] inline Status gpio_read(unsigned int pin, bool& high) {
    return platform().gpio_read(pin, high);
}

}
