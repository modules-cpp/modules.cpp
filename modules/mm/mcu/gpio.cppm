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

[[nodiscard]] inline Status gpio_watch(unsigned int pin, Pull pull, Edge edge) {
    if (edge != Edge::Rising && edge != Edge::Falling && edge != Edge::Both)
        return Status::BadArgument;
    return platform().gpio_watch(pin, pull, edge);
}

[[nodiscard]] inline Status gpio_take(unsigned int pin, bool& pending) {
    return platform().gpio_take(pin, pending);
}

[[nodiscard]] inline Status gpio_unwatch(unsigned int pin) {
    return platform().gpio_unwatch(pin);
}

[[nodiscard]] inline Status gpio_wait(unsigned int pin, unsigned long timeout_ms,
                                      bool& pending) {
    return platform().gpio_wait(pin, timeout_ms, pending);
}

}
