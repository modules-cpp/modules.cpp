// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Sample arithmetic. C++20 defines shifts of negative values as two's
// complement, so a right shift here is arithmetic and a left shift is a
// multiplication; the sixty-four-bit intermediates keep both in range.
module;

#include <cstdint>
#include <limits>

module mm.audio;

namespace mm::audio {

namespace {

constexpr bool usable(unsigned int bits) { return bits >= 1 && bits <= 32; }

std::int16_t saturate(std::int64_t value) {
    constexpr std::int64_t high = std::numeric_limits<std::int16_t>::max();
    constexpr std::int64_t low = std::numeric_limits<std::int16_t>::min();
    if (value > high) return static_cast<std::int16_t>(high);
    if (value < low) return static_cast<std::int16_t>(low);
    return static_cast<std::int16_t>(value);
}

// value, taken as bits wide, scaled to sixteen bits.
std::int16_t scale_to_sample(std::int64_t value, unsigned int bits) {
    const std::int64_t high = (std::int64_t{1} << (bits - 1)) - 1;
    const std::int64_t low = -(std::int64_t{1} << (bits - 1));
    if (value > high) value = high;
    if (value < low) value = low;
    if (bits <= 16) return saturate(value << (16 - bits));
    return saturate(value >> (bits - 16));
}

}  // namespace

std::int16_t to_sample(std::int32_t value, unsigned int bits) {
    if (!usable(bits)) return 0;
    return scale_to_sample(value, bits);
}

std::int16_t to_sample_offset(std::uint32_t count, unsigned int bits) {
    if (!usable(bits)) return 0;
    const std::int64_t half = std::int64_t{1} << (bits - 1);
    return scale_to_sample(std::int64_t{count} - half, bits);
}

std::int32_t from_sample(std::int16_t sample, unsigned int bits) {
    if (!usable(bits)) return 0;
    if (bits >= 16) return static_cast<std::int32_t>(std::int64_t{sample} << (bits - 16));
    return static_cast<std::int32_t>(sample >> (16 - bits));
}

std::uint32_t from_sample_offset(std::int16_t sample, unsigned int bits) {
    if (!usable(bits)) return 0;
    const std::int64_t half = std::int64_t{1} << (bits - 1);
    return static_cast<std::uint32_t>(std::int64_t{from_sample(sample, bits)} + half);
}

}
