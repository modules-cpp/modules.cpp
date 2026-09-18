// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <optional>
#include <span>
#include <string_view>

export module mm.mcu:adc_types;

export namespace mm::mcu {

// One input the converter can sample. number is what the ADC facility
// accepts; gpio is the pad the channel is attached to, absent for an internal
// source such as a temperature sensor. bits is the width of a count, so a
// reading lies in [0, 2^bits); reference_millivolts is what a full-scale count
// denotes when the board knows it, and zero when it does not, which still
// leaves the raw count readable. Each channel carries its own, because a
// Linux IIO device may scale its channels differently and one converter-wide
// value would lie about some of them.
struct AdcChannel {
    unsigned int number = 0;
    std::string_view name;
    std::optional<unsigned int> gpio;
    unsigned int bits = 0;
    unsigned int reference_millivolts = 0;
};

// The inventory, viewing provider-owned static storage as Board::gpios does.
// A number occurs once, and an attached GPIO at most once.
struct AdcDescription {
    std::span<const AdcChannel> channels;
};

}
