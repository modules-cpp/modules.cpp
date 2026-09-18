// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The portable e-paper display for the emulated Linux board: the
// pico_epaper_b descriptor and registration pattern, with the board's pin
// numbers so the real controller talks the same bytes the chip expects, on
// SPI instance 0 of the emulated single bus. The panel is the tri-color one:
// a black/white plane plus a pigment plane at 0x26, exactly as the PICO
// board declares it.
module;

#include <array>
#include <cstddef>
#include <optional>

export module platform.linux.epaper.display;

import mm.display;
import mm.epaper.ssd1680;
import mm.mcu;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::epaper_display_provider {

constexpr std::array border_data{std::byte{0x01}};
constexpr std::array initialization{
    mm::epaper::ssd1680::InitializationCommand{std::byte{0x3c}, border_data},
};

constexpr mm::epaper::ssd1680::Wiring wiring{
    .spi = {.instance = 0,
            .clock_gpio = 10,
            .transmit_gpio = 11,
            .receive_gpio = std::nullopt,
            .baud = 4'000'000,
            .mode = mm::mcu::SpiMode::Mode0,
            .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
    .chip_select_gpio = 9,
    .data_command_gpio = 8,
    .reset_gpio = 12,
    .busy_gpio = 13,
    .reset_active_low = true,
    .busy_active_high = true,
    .reset_hold_ms = 2,
    .reset_release_ms = 200,
    .busy_timeout_ms = 10'000,
};

constexpr mm::epaper::ssd1680::Panel panel{
    .width = 152,
    .height = 296,
    .initialization = initialization,
    .full_update_control = std::nullopt,
    .partial_update_control = std::nullopt,
    .chromatic_ram_command = std::byte{0x26},
    .deep_sleep_control = std::byte{0x01},
};

mm::epaper::ssd1680::Controller display{wiring, panel};

struct Register {
    Register() { mm::display::set_display(display); }
};

const Register registered;

}  // namespace
