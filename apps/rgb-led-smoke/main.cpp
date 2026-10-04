// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Something a person can look at, for an LED rather than a panel. Red, green,
// blue, and white on the whole chain, each held long enough to be named, then
// a colour wheel turned through the chain, then dark.
//
// It names mm.led and mm.mcu and no board, so it runs wherever both are bound.
#include <array>
#include <cstdint>
#include <span>

import mm.led;
import mm.mcu;

namespace {

// A chain longer than this shows the pattern on its first most_leds LEDs.
constexpr unsigned int most_leds = 64;
std::array<mm::led::Color, most_leds> colors;

// A quarter of full scale: plainly visible, not uncomfortable.
constexpr std::uint8_t level = 64;

constexpr unsigned int hold_ms = 1'000;
constexpr unsigned int wheel_steps = 96;
constexpr unsigned int wheel_step_ms = 40;

// A hue on a six-sector wheel of wheel_steps steps, at the chosen level.
mm::led::Color wheel(unsigned int step) {
    const unsigned int sector_steps = wheel_steps / 6;
    const unsigned int sector = (step % wheel_steps) / sector_steps;
    const unsigned int within = (step % wheel_steps) % sector_steps;
    const auto rising = static_cast<std::uint8_t>(level * within / sector_steps);
    const auto falling = static_cast<std::uint8_t>(level - rising);
    switch (sector) {
        case 0: return {level, rising, 0};
        case 1: return {falling, level, 0};
        case 2: return {0, level, rising};
        case 3: return {0, falling, level};
        case 4: return {rising, 0, level};
        default: return {level, 0, falling};
    }
}

bool show(mm::led::Led& led, unsigned int count) {
    return led.write(0, std::span<const mm::led::Color>{colors.data(), count}) ==
               mm::led::Status::Ok &&
           led.refresh() == mm::led::Status::Ok;
}

}  // namespace

int main() {
    auto& led = mm::led::selected_led();

    if (led.initialize() != mm::led::Status::Ok) return 1;
    const unsigned int count = led.count() < most_leds ? led.count() : most_leds;
    if (count == 0) return 2;

    constexpr std::array<mm::led::Color, 4> solids{{
        {level, 0, 0},
        {0, level, 0},
        {0, 0, level},
        {level, level, level},
    }};
    for (const auto& solid : solids) {
        for (unsigned int i = 0; i < count; ++i) colors[i] = solid;
        if (!show(led, count)) return 3;
        if (mm::mcu::delay_ms(hold_ms) != mm::mcu::Status::Ok) return 5;
    }

    // Each LED a step further round the wheel than the one before it, so a
    // chain shows a moving rainbow and a single LED a slow change of hue.
    for (unsigned int step = 0; step < 2 * wheel_steps; ++step) {
        for (unsigned int i = 0; i < count; ++i)
            colors[i] = wheel(step + i * wheel_steps / count);
        if (!show(led, count)) return 4;
        if (mm::mcu::delay_ms(wheel_step_ms) != mm::mcu::Status::Ok) return 5;
    }

    if (led.sleep() != mm::led::Status::Ok) return 6;
    return 0;
}
