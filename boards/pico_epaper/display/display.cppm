// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <optional>

export module platform.pico_epaper.display;

import mm.display;
import mm.epaper.ssd1680;
import mm.mcu;

namespace {

// Waveshare's Pico-specific reference driver selects border waveform 0x01 for
// the full-refresh path. The generic controller owns reset, RAM addressing,
// data-entry mode, and the remaining SSD1680 command sequence.
constexpr std::array border_data{std::byte{0x01}};
constexpr std::array initialization{
    mm::epaper::ssd1680::InitializationCommand{std::byte{0x3c}, border_data},
};

constexpr mm::epaper::ssd1680::Wiring wiring{
    .spi = {.instance = 1,
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
    .full_update_control = std::byte{0xf7},
    .partial_update_control = std::nullopt,
    .deep_sleep_control = std::byte{0x01},
};

mm::epaper::ssd1680::Controller display{wiring, panel};

struct Register {
    Register() { mm::display::set_display(display); }
};

const Register registered;

}  // namespace
