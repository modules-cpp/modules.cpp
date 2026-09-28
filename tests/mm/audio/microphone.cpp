// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

import mm.mcu;
import mm.audio;
import mm.audio.microphone;
import mm.test;

void mm_test_mic_reset();
void mm_test_mic_force(mm::mcu::Status status);
void mm_test_mic_raw(unsigned int value);
unsigned int mm_test_mic_reads();
unsigned int mm_test_mic_configured(std::size_t index);
bool mm_test_mic_released();

namespace {

using mm::test::expect;
using mm::audio::Status;

mm::audio::microphone::Wiring wiring() {
    return {.channel = 0, .rate = 44'100, .bits = 12};
}

void initializes_and_describes_the_microphone() {
    mm_test_mic_reset();
    mm::audio::microphone::Microphone microphone{wiring()};
    expect(microphone.initialize() == Status::Ok, "a valid microphone initializes");
    expect(mm_test_mic_configured(0) == 0, "the channel the provider named is claimed");

    const auto description = microphone.description();
    expect(description.name == "microphone" && description.rate == 44'100 &&
               description.channels == 1 &&
               description.direction == mm::audio::Direction::Input,
           "the description reports the input the provider described");
}

// A microphone's output is centred on the ADC's half scale, so the same count
// maps to a sample near zero for a quiet input and to a large value at the
// edges, all within sixteen bits.
void capture_centres_and_scales_to_sixteen_bits() {
    mm_test_mic_reset();
    mm::audio::microphone::Microphone microphone{wiring()};
    expect(microphone.initialize() == Status::Ok, "the microphone initializes");

    // Half-scale: 12-bit, half = 2048, centred to zero.
    mm_test_mic_raw(2048);
    std::array<mm::audio::Sample, 4> samples{};
    std::size_t count = 0;
    expect(microphone.capture(samples, count) == Status::Ok,
           "a capture into the caller's span succeeds");
    expect(count == 4 && mm_test_mic_reads() == 4,
           "the capture draws one conversion per sample");
    expect(samples[0] == 0 && samples[3] == 0, "half-scale is a sample near zero");

    // Above mid-scale: 3072, centred to 1024, scaled to 16384.
    mm_test_mic_raw(3072);
    expect(microphone.capture(samples, count) == Status::Ok, "a second capture");
    expect(samples[0] == 16'384 && samples[3] == 16'384,
           "a count above mid-scale is a positive sixteen-bit sample");

    // Below mid-scale: 1024, centred to -1024, scaled to -16384.
    mm_test_mic_raw(1024);
    expect(microphone.capture(samples, count) == Status::Ok, "a third capture");
    expect(samples[0] == -16'384 && samples[3] == -16'384,
           "a count below mid-scale is a negative sixteen-bit sample");
}

void capture_before_initialize_reports_not_initialized() {
    mm_test_mic_reset();
    mm::audio::microphone::Microphone microphone{wiring()};
    std::array<mm::audio::Sample, 2> samples{};
    std::size_t count = 0;
    expect(microphone.capture(samples, count) == Status::NotInitialized,
           "no capture before the channel is claimed");
}

void rejects_invalid_wiring() {
    mm_test_mic_reset();
    mm::audio::microphone::Microphone no_rate{
        {.channel = 0, .rate = 0, .bits = 12}};
    expect(no_rate.initialize() == Status::BadArgument, "a microphone without a rate is refused");

    mm::audio::microphone::Microphone narrow{
        {.channel = 0, .rate = 44'100, .bits = 0}};
    expect(narrow.initialize() == Status::BadArgument,
           "a width that cannot be scaled to sixteen bits is refused");

    mm::audio::microphone::Microphone wide{
        {.channel = 0, .rate = 44'100, .bits = 17}};
    expect(wide.initialize() == Status::BadArgument,
           "a width the scale cannot hold is refused");
}

void rejects_a_channel_the_adc_does_not_offer() {
    mm_test_mic_reset();
    mm::audio::microphone::Microphone out_of_range{
        {.channel = 8, .rate = 44'100, .bits = 12}};
    expect(out_of_range.initialize() == Status::BadArgument,
           "a channel the converter does not have is refused");
    expect(mm_test_mic_configured(0) == 9999, "no channel is claimed in the refusal");
}

void shutdown_releases_the_channel() {
    mm_test_mic_reset();
    mm::audio::microphone::Microphone microphone{wiring()};
    expect(microphone.initialize() == Status::Ok, "the microphone initializes");
    expect(microphone.shutdown() == Status::Ok, "shutdown succeeds once initialized");
    expect(mm_test_mic_released(), "the channel is released");
    expect(microphone.shutdown() == Status::NotInitialized,
           "a second shutdown is not initialized");
}

const mm::test::case_ cases[] = {
    {"microphone initializes and describes itself", &initializes_and_describes_the_microphone},
    {"capture centres and scales to sixteen bits", &capture_centres_and_scales_to_sixteen_bits},
    {"capture before initialize", &capture_before_initialize_reports_not_initialized},
    {"microphone rejects invalid wiring", &rejects_invalid_wiring},
    {"microphone rejects a channel the adc does not offer", &rejects_a_channel_the_adc_does_not_offer},
    {"shutdown releases the channel", &shutdown_releases_the_channel},
};

const mm::test::registrar reg{"mm.audio.microphone", cases};

}  // namespace
