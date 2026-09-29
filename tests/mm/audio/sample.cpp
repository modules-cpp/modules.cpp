// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdint>
#include <limits>

import mm.audio;
import mm.test;

namespace {

using mm::test::expect;

constexpr std::int16_t sample_max = std::numeric_limits<std::int16_t>::max();
constexpr std::int16_t sample_min = std::numeric_limits<std::int16_t>::min();

void widths_outside_the_range_answer_zero() {
    expect(mm::audio::to_sample(100, 0) == 0 && mm::audio::to_sample(100, 33) == 0,
           "a signed value of no width, or more than thirty-two, is zero");
    expect(mm::audio::to_sample_offset(100, 0) == 0 &&
               mm::audio::to_sample_offset(100, 33) == 0,
           "so is an offset count");
    expect(mm::audio::from_sample(100, 0) == 0 && mm::audio::from_sample(100, 33) == 0 &&
               mm::audio::from_sample_offset(100, 0) == 0 &&
               mm::audio::from_sample_offset(100, 33) == 0,
           "and so are the inverses");
}

// At every width the extremes and zero land where a sample's do, and the
// round trip through the wire format is exact wherever the wire is at least
// sixteen bits wide.
void every_width_maps_its_extremes() {
    for (unsigned int bits = 1; bits <= 32; ++bits) {
        const std::int64_t high = (std::int64_t{1} << (bits - 1)) - 1;
        const std::int64_t low = -(std::int64_t{1} << (bits - 1));
        const auto top = mm::audio::to_sample(static_cast<std::int32_t>(high), bits);
        const auto bottom = mm::audio::to_sample(static_cast<std::int32_t>(low), bits);
        expect(bottom == sample_min, "the most negative value is the most negative sample");
        expect(top > 0 || bits == 1, "the most positive value is positive");
        expect(mm::audio::to_sample(0, bits) == 0, "zero is silence");
        expect(mm::audio::to_sample_offset(static_cast<std::uint32_t>(-low), bits) == 0,
               "half of an offset converter's full scale is silence");
        expect(mm::audio::to_sample_offset(0, bits) == sample_min,
               "an offset converter's zero is the most negative sample");
        expect(mm::audio::from_sample_offset(0, bits) == static_cast<std::uint32_t>(-low),
               "silence is half scale on an offset output");
        expect(mm::audio::from_sample(sample_min, bits) == low,
               "the most negative sample is the most negative value");
        expect(mm::audio::from_sample(sample_max, bits) <= high,
               "the most positive sample fits the width");
        if (bits >= 16) {
            constexpr std::int16_t samples[] = {sample_min, -1, 0, 1, 12345, sample_max};
            for (const auto sample : samples) {
                expect(mm::audio::to_sample(mm::audio::from_sample(sample, bits), bits) ==
                           sample,
                       "a sample survives a round trip through a wide signed word");
                expect(mm::audio::to_sample_offset(mm::audio::from_sample_offset(sample, bits),
                                                   bits) == sample,
                       "and through a wide offset level");
            }
        }
    }
}

void narrow_values_are_shifted_up_and_wide_ones_down() {
    expect(mm::audio::to_sample(2047, 12) == 2047 * 16, "twelve bits shift up by four");
    expect(mm::audio::to_sample_offset(2048, 12) == 0 &&
               mm::audio::to_sample_offset(4095, 12) == 2047 * 16 &&
               mm::audio::to_sample_offset(0, 12) == sample_min,
           "a twelve-bit SAR count centres on half scale");
    expect(mm::audio::to_sample(0x123456, 24) == 0x1234,
           "twenty-four bits keep their upper sixteen");
    expect(mm::audio::to_sample(-(1 << 23), 24) == sample_min,
           "and the most negative stays most negative");
    expect(mm::audio::from_sample(0x1234, 24) == 0x123400,
           "a sample widens to twenty-four bits by shifting");
    expect(mm::audio::from_sample(sample_max, 10) == 511 &&
               mm::audio::from_sample(sample_min, 10) == -512,
           "narrowing keeps the upper bits");
    expect(mm::audio::from_sample_offset(0, 10) == 512 &&
               mm::audio::from_sample_offset(sample_max, 10) == 1023 &&
               mm::audio::from_sample_offset(sample_min, 10) == 0,
           "a ten-bit PWM level spans the whole counter with silence at half");
}

void out_of_range_values_saturate() {
    expect(mm::audio::to_sample(5000, 12) == 2047 * 16,
           "a value above a twelve-bit range saturates at its top value");
    expect(mm::audio::to_sample(-5000, 12) == sample_min, "and below it saturates low");
    expect(mm::audio::to_sample_offset(9000, 12) == 2047 * 16,
           "a count above full scale saturates at the top count");
    expect(mm::audio::to_sample(std::numeric_limits<std::int32_t>::max(), 32) == sample_max &&
               mm::audio::to_sample(std::numeric_limits<std::int32_t>::min(), 32) == sample_min,
           "a thirty-two-bit word's extremes are the sample's");
}

const mm::test::case_ cases[] = {
    {"widths outside the range answer zero", &widths_outside_the_range_answer_zero},
    {"every width maps its extremes", &every_width_maps_its_extremes},
    {"narrow shift up, wide shift down", &narrow_values_are_shifted_up_and_wide_ones_down},
    {"out of range values saturate", &out_of_range_values_saturate},
};

const mm::test::registrar reg{"mm.audio sample", cases};

}  // namespace
