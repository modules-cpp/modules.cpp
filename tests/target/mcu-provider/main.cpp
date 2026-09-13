// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
import mm.mcu;

int main() {
    const auto status =
        mm::mcu::gpio_configure(25, mm::mcu::Direction::Out, mm::mcu::Pull::None);
    return status == mm::mcu::Status::BadArgument ? 1 : 0;
}
