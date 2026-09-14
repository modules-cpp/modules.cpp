// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <optional>

export module platform.pico_epaper_b.display;

import mm.display;
import mm.epaper.ssd1680;
import mm.mcu;

namespace {

// Waveshare's 2.66-B reference configures display-update control 1 and then
// starts a full refresh directly with master activation. Image writes use
// black/white RAM 0x24 and complemented red RAM 0x26.
constexpr std::array update_control_data{std::byte{0x00}, std::byte{0x80}};
constexpr std::array initialization{
    mm::epaper::ssd1680::InitializationCommand{
        std::byte{0x21}, update_control_data},
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
    .busy_timeout_ms = 30'000,
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
