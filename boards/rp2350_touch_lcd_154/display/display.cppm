// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <optional>

export module platform.rp2350_touch_lcd_154.display;

import mm.display;
import mm.lcd.st7789;
import mm.mcu;

namespace {

// Porch, power, and gamma, as Waveshare's LCD_1in54 driver sends them. These
// are panel settings rather than controller defaults, which is why the board
// supplies them and mm.lcd.st7789 does not.
constexpr std::array porch{std::byte{0x0c}, std::byte{0x0c}, std::byte{0x00},
                           std::byte{0x33}, std::byte{0x33}};
constexpr std::array gate{std::byte{0x35}};
constexpr std::array vcom{std::byte{0x19}};
constexpr std::array control{std::byte{0x2c}};
constexpr std::array power2{std::byte{0x01}};
constexpr std::array power3{std::byte{0x12}};
constexpr std::array vrh{std::byte{0x20}};
constexpr std::array vdv{std::byte{0x0f}};
constexpr std::array power_control{std::byte{0xa4}, std::byte{0xa1}};
constexpr std::array gamma_positive{
    std::byte{0x04}, std::byte{0x0d}, std::byte{0x11}, std::byte{0x13},
    std::byte{0x2b}, std::byte{0x3f}, std::byte{0x54}, std::byte{0x4c},
    std::byte{0x18}, std::byte{0x0d}, std::byte{0x0b}, std::byte{0x1f},
    std::byte{0x23}};
constexpr std::array gamma_negative{
    std::byte{0x04}, std::byte{0x0c}, std::byte{0x11}, std::byte{0x13},
    std::byte{0x2c}, std::byte{0x3f}, std::byte{0x44}, std::byte{0x51},
    std::byte{0x2f}, std::byte{0x1f}, std::byte{0x1f}, std::byte{0x20},
    std::byte{0x23}};

constexpr std::array initialization{
    mm::lcd::st7789::InitializationCommand{std::byte{0xb2}, porch},
    mm::lcd::st7789::InitializationCommand{std::byte{0xb7}, gate},
    mm::lcd::st7789::InitializationCommand{std::byte{0xbb}, vcom},
    mm::lcd::st7789::InitializationCommand{std::byte{0xc0}, control},
    mm::lcd::st7789::InitializationCommand{std::byte{0xc2}, power2},
    mm::lcd::st7789::InitializationCommand{std::byte{0xc3}, power3},
    mm::lcd::st7789::InitializationCommand{std::byte{0xc4}, vrh},
    mm::lcd::st7789::InitializationCommand{std::byte{0xc6}, vdv},
    mm::lcd::st7789::InitializationCommand{std::byte{0xd0}, power_control},
    mm::lcd::st7789::InitializationCommand{std::byte{0xe0}, gamma_positive},
    mm::lcd::st7789::InitializationCommand{std::byte{0xe1}, gamma_negative},
};

constexpr mm::lcd::st7789::Wiring wiring{
    .spi = {.instance = 1,
            .clock_gpio = 10,
            .transmit_gpio = 11,
            .receive_gpio = std::nullopt,
            .baud = 230'000'000,
            .mode = mm::mcu::SpiMode::Mode0,
            .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
    .chip_select_gpio = 9,
    .data_command_gpio = 12,
    .reset_gpio = 14,
    .backlight_gpio = 13,
    .reset_step_ms = 100,
    .sleep_out_ms = 120,
};

constexpr mm::lcd::st7789::Panel panel{
    .width = 240,
    .height = 240,
    .initialization = initialization,
    .memory_access = std::byte{0x00},
    .inverted = false,
    .column_offset = 0,
    .row_offset = 0,
};

mm::lcd::st7789::Controller display{wiring, panel};

struct Register {
    Register() { mm::display::set_display(display); }
};

const Register registered;

}  // namespace
