// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

export module mm.mcu:dac_types;

export namespace mm::mcu {

// One output that holds a level. number is what the DAC facility accepts;
// gpio is the pad it drives when it drives one. bits is the width of a level,
// so a level lies in [0, 2^bits) and half scale is 2^(bits - 1). pwm is true
// where the output is a modulator behind a filter rather than a converter,
// which a board choosing its filter needs to know. The rate limits are hertz
// and depth the number of levels the platform queues; a provider that cannot
// know a limit reports zero, which means unknown rather than unlimited.
struct DacOutput {
    unsigned int number = 0;
    std::string_view name;
    std::optional<unsigned int> gpio;
    unsigned int bits = 0;
    bool pwm = false;
    unsigned long minimum_rate_hz = 0;
    unsigned long maximum_rate_hz = 0;
    std::size_t depth = 0;
};

// The inventory, viewing provider-owned static storage as Board::gpios does.
// A number occurs once, and an attached GPIO at most once.
struct DacDescription {
    std::span<const DacOutput> outputs;
};

}
