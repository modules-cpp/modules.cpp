// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

export module mm.mcu:pwm_types;

export namespace mm::mcu {

// One output the modulator can drive. number is what the PWM facility
// accepts; gpio is the pad it drives when it drives one. group names the
// counter the output shares its period with -- a Pico slice, say -- and
// comparator the compare register within that counter, so that two outputs
// in one group are siblings at one period and two with one comparator are
// aliases of one signal. The period limits are nanoseconds and per output;
// a provider that cannot know them reports both zero, which means unknown
// rather than unlimited.
struct PwmOutput {
    unsigned int number = 0;
    std::string_view name;
    std::optional<unsigned int> gpio;
    unsigned int group = 0;
    unsigned int comparator = 0;
    std::uint64_t minimum_period_ns = 0;
    std::uint64_t maximum_period_ns = 0;
};

struct PwmDescription {
    std::span<const PwmOutput> outputs;
};

// A counter whose period is clock ticks times a divider times (top + 1), with
// the divider a fixed-point value of the stated integer and fraction bits and
// the compare register the counter's own width. That is the RP2040's and the
// RP2350's shape and most microcontrollers'; pwm_plan turns a period into a
// divider and a top for it. A counter of another shape plans for itself.
struct PwmCounter {
    std::uint64_t clock_hz = 0;
    unsigned int counter_bits = 0;
    unsigned int divider_integer_bits = 0;
    unsigned int divider_fraction_bits = 0;
};

// top is the last count before wrap; divider is in units of
// 2^-divider_fraction_bits; actual_period_ns is what the counter will run.
struct PwmPlan {
    unsigned int top = 0;
    unsigned int divider = 0;
    std::uint64_t actual_period_ns = 0;
};

}
