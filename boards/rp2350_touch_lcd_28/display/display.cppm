// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <optional>

export module platform.rp2350_touch_lcd_28.display;

import mm.display;
import mm.lcd.st7789;
import mm.mcu;

namespace {

// Porch, power, and gamma, as Waveshare's LCD_2in8 driver sends them. These
// are panel settings rather than controller defaults, which is why the board
// supplies them and mm.lcd.st7789 does not.
constexpr std::array porch{std::byte{0x0c}, std::byte{0x0c}, std::byte{0x00},
                           std::byte{0x33}, std::byte{0x33}};
constexpr std::array gate{std::byte{0x75}};
constexpr std::array vcom{std::byte{0x1a}};
constexpr std::array control{std::byte{0x2c}};
constexpr std::array power2{std::byte{0x01}, std::byte{0xff}};
constexpr std::array power3{std::byte{0x13}};
constexpr std::array vrh{std::byte{0x20}};
constexpr std::array vdv{std::byte{0x0f}};
constexpr std::array power_control{std::byte{0xa4}, std::byte{0xa1}};
constexpr std::array power_control2{std::byte{0xa1}};
constexpr std::array gamma_positive{
    std::byte{0xd0}, std::byte{0x0d}, std::byte{0x14}, std::byte{0x0d},
    std::byte{0x0d}, std::byte{0x09}, std::byte{0x38}, std::byte{0x44},
    std::byte{0x4e}, std::byte{0x3a}, std::byte{0x17}, std::byte{0x18},
    std::byte{0x2f}, std::byte{0x30}};
constexpr std::array gamma_negative{
    std::byte{0xd0}, std::byte{0x09}, std::byte{0x0f}, std::byte{0x08},
    std::byte{0x07}, std::byte{0x14}, std::byte{0x37}, std::byte{0x44},
    std::byte{0x4d}, std::byte{0x38}, std::byte{0x15}, std::byte{0x16},
    std::byte{0x2c}, std::byte{0x2e}};

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
    mm::lcd::st7789::InitializationCommand{std::byte{0xd6}, power_control2},
    mm::lcd::st7789::InitializationCommand{std::byte{0xe0}, gamma_positive},
    mm::lcd::st7789::InitializationCommand{std::byte{0xe1}, gamma_negative},
};

constexpr mm::lcd::st7789::Wiring wiring{
    .spi = {.instance = 1,
            .clock_gpio = 10,
            .transmit_gpio = 11,
            .receive_gpio = std::nullopt,
            .baud = 62'500'000,
            .mode = mm::mcu::SpiMode::Mode0,
            .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
    .chip_select_gpio = 13,
    .data_command_gpio = 14,
    .reset_gpio = 15,
    .backlight_gpio = 16,
    .reset_step_ms = 100,
    .sleep_out_ms = 120,
};

constexpr mm::lcd::st7789::Panel panel{
    .width = 240,
    .height = 320,
    .initialization = initialization,
    .memory_access = std::byte{0x00},
    .inverted = true,
    .column_offset = 0,
    .row_offset = 0,
};

mm::lcd::st7789::Controller display{wiring, panel};

struct Register {
    Register() { mm::display::set_display(display); }
};

const Register registered;

}  // namespace
