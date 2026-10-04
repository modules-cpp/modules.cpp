// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform for a pulse output: the configuration the
// controller claimed and every frame it sent, which is the whole of an LED
// driver's observable behaviour.
#include <cstddef>
#include <span>
#include <vector>

import mm.mcu;

namespace {

class RecordingPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status pulse_configure(
        const mm::mcu::PulseConfiguration& value) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        configuration = value;
        configured = true;
        ++configures;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pulse_write(unsigned int instance,
                                              std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || instance != configuration.instance)
            return mm::mcu::Status::BadArgument;
        frames.emplace_back(data.begin(), data.end());
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pulse_release(unsigned int) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        configured = false;
        ++releases;
        return mm::mcu::Status::Ok;
    }

    void reset() { *this = RecordingPlatform{}; }

    mm::mcu::PulseConfiguration configuration;
    bool configured = false;
    unsigned int configures = 0;
    unsigned int releases = 0;
    std::vector<std::vector<std::byte>> frames;
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
void mm_test_led_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_led_force(mm::mcu::Status status) { platform.forced = status; }
mm::mcu::PulseConfiguration mm_test_led_configuration() { return platform.configuration; }
bool mm_test_led_configured() { return platform.configured; }
unsigned int mm_test_led_configures() { return platform.configures; }
unsigned int mm_test_led_releases() { return platform.releases; }
std::size_t mm_test_led_frames() { return platform.frames.size(); }
std::size_t mm_test_led_frame_size(std::size_t index) {
    return index < platform.frames.size() ? platform.frames[index].size() : 0;
}
unsigned int mm_test_led_byte(std::size_t index, std::size_t offset) {
    if (index >= platform.frames.size()) return 0x100;
    const auto& frame = platform.frames[index];
    return offset < frame.size() ? static_cast<unsigned int>(frame[offset]) : 0x100;
}
