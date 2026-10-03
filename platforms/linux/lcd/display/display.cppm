// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The portable colour display for the emulated Linux board: mm.lcd.st7789's
// controller with the board's pins, so the real driver talks the same bytes
// the chip expects, on SPI instance 0. The emulated glass is mounted a half
// turn round, as Adafruit's modules are, so the panel descriptor sets MX and
// MY to draw upright, and it is an IPS glass, so inversion is on.
module;

#include <cstddef>
#include <optional>

export module platform.linux.lcd.display;

import mm.display;
import mm.lcd.st7789;
import mm.mcu;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::lcd_display_provider {

constexpr mm::lcd::st7789::Wiring wiring{
    .spi = {.instance = 0,
            .clock_gpio = 10,
            .transmit_gpio = 11,
            .receive_gpio = std::nullopt,
            .baud = 32'000'000,
            .mode = mm::mcu::SpiMode::Mode0,
            .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
    .chip_select_gpio = 9,
    .data_command_gpio = 6,
    .reset_gpio = 5,
    .backlight_gpio = std::nullopt,
    .reset_step_ms = 10,
    .sleep_out_ms = 10,
};

// The emulated controller needs none of the porch, power, or gamma settings
// a real glass does.
constexpr mm::lcd::st7789::Panel panel{
    .width = 240,
    .height = 320,
    .initialization = {},
    .memory_access = std::byte{0xc0},
    .inverted = true,
    .column_offset = 0,
    .row_offset = 0,
};

mm::lcd::st7789::Controller display{wiring, panel};

struct Register {
    Register() { mm::display::set_display(display); }
};

const Register registered;

}  // namespace platform::linux::lcd_display_provider
