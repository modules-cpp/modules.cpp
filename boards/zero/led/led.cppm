// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>

export module platform.zero.led;

import mm.led;
import mm.led.ws2812b;
import mm.mcu;

namespace {

constexpr unsigned int leds = 1;

std::array<std::byte, 3 * leds> frame{};

constexpr mm::led::ws2812b::Wiring wiring{
    .instance = 0,
    .gpio = 16,
};

mm::led::ws2812b::Controller led{wiring, {.count = leds,
                                          .order = mm::led::ws2812b::Order::Grb,
                                          .frame = frame}};

struct Register {
    Register() { mm::led::set_led(led); }
};

const Register registered;

}  // namespace
