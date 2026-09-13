// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
import mm.mcu;

int main() {
    if (mm::mcu::gpio_configure(25, mm::mcu::Direction::Out, mm::mcu::Pull::None) !=
        mm::mcu::Status::Ok)
        return 1;

    // delay_ms is Unsupported on a platform that keeps no clock, and the loop
    // still blinks as fast as the board can: an application decides what an
    // unsupported facility means to it.
    for (int i = 0; i < 4; ++i) {
        if (mm::mcu::gpio_write(25, i % 2 == 0) != mm::mcu::Status::Ok) return 1;
        (void)mm::mcu::delay_ms(500);
    }
    return 0;
}
