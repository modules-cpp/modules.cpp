// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:pulse_types;

export namespace mm::mcu {

// A one-wire pulse-width-coded output: one GPIO, no clock, each bit a fixed
// period that starts high and stays high for zero_high_ns when the bit is zero
// or one_high_ns when it is one, then low for the rest of the period. Bytes go
// most significant bit first. After the last bit the line stays low for at
// least reset_ns, which is what a WS2812-style device takes as the end of a
// frame. Times are what the platform aims for; it gets as near as its clock
// allows, and a configuration it cannot get near is BadArgument.
struct PulseConfiguration {
    unsigned int instance = 0;
    unsigned int gpio = 0;
    std::uint32_t bit_period_ns = 0;
    std::uint32_t zero_high_ns = 0;
    std::uint32_t one_high_ns = 0;
    std::uint32_t reset_ns = 0;
};

}
