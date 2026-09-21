// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026

import mm.mcu;

int main() {
    const auto description = mm::mcu::board();
    if (!description.led) return 1;
    const auto& led = *description.led;

    if (mm::mcu::gpio_configure(led.gpio, mm::mcu::Direction::Out, mm::mcu::Pull::None) !=
        mm::mcu::Status::Ok)
        return 1;

    // delay_ms is Unsupported on a platform that keeps no clock, and the loop
    // still blinks as fast as the board can: an application decides what an
    // unsupported facility means to it.
    for (int i = 0; i < 4; ++i) {
        const bool on = i % 2 == 0;
        if (mm::mcu::gpio_write(led.gpio, on == led.active_high) != mm::mcu::Status::Ok)
            return 1;
        (void)mm::mcu::delay_ms(500);
    }
    return 0;
}
