// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as an ADC does. It is a stand-in for
// the converter: the driver's correctness is which conversions it draws and
// what it makes of each count, both of which are observable here without a
// microphone.
#include <cstddef>
#include <span>
#include <vector>

import mm.mcu;

namespace {

// The stand-in converter offers the eight external channels 0 to 7, as the
// RP2350's ADC does; a number outside that range is one it does not have.
constexpr unsigned int adc_channel_max = 7;

class RecordingPlatform : public mm::mcu::Platform {
public:
    RecordingPlatform() { reset(); }

    void reset() {
        adc_configured.clear();
        adc_reads = 0;
        adc_released = false;
        next_raw = 0;
        ticks = 0;
        forced = mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_configure(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (channel > adc_channel_max) return mm::mcu::Status::BadArgument;
        adc_configured.push_back(channel);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_read(unsigned int channel, unsigned int& count) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!adc_configured_at(channel)) return mm::mcu::Status::BadArgument;
        ++adc_reads;
        count = next_raw;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_release(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!adc_configured_at(channel)) return mm::mcu::Status::BadArgument;
        adc_released = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += milliseconds;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] bool adc_configured_at(unsigned int channel) const {
        for (auto offered : adc_configured)
            if (offered == channel) return true;
        return false;
    }

    std::vector<unsigned int> adc_configured;
    unsigned int adc_reads = 0;
    bool adc_released = false;
    unsigned int next_raw = 0;
    unsigned long ticks = 0;
    mm::mcu::Status forced = mm::mcu::Status::Ok;
};

RecordingPlatform platform;

struct Register {
    Register() { mm::mcu::set_platform(platform); }
};

const Register registered;

}  // namespace

// A configured board may inject its own mm.mcu platform into this binary,
// and its static registration may run after ours. Reclaim the seam so every
// case deterministically exercises the driver through this fake.
void mm_test_mic_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_mic_force(mm::mcu::Status status) { platform.forced = status; }
void mm_test_mic_raw(unsigned int value) { platform.next_raw = value; }
unsigned int mm_test_mic_reads() { return platform.adc_reads; }
unsigned int mm_test_mic_configured(std::size_t index) {
    return index < platform.adc_configured.size() ? platform.adc_configured[index] : 9999;
}
bool mm_test_mic_released() { return platform.adc_released; }
