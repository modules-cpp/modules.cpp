// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as an ES8311 does. It is a stand-in
// for the codec and the buses together: the driver's correctness is the
// registers it programs and the bytes it ships down the I2S link, both of
// which are observable here without a codec.
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

import mm.mcu;

namespace {

// The address the stand-in codec answers on, matching the driver's default.
constexpr unsigned int device_address = 0x06;

class RecordingPlatform : public mm::mcu::Platform {
public:
    RecordingPlatform() { reset(); }

    void reset() {
        i2c_configured = false;
        i2s_configured = false;
        i2c_writes.clear();
        i2s_written.clear();
        ticks = 0;
        forced = mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.baud == 0) return mm::mcu::Status::BadArgument;
        i2c_configured = true;
        return mm::mcu::Status::Ok;
    }

    // A write is a register byte and its value, the only kind of write this
    // codec takes.
    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2c_configured || address != device_address || data.size() != 2)
            return mm::mcu::Status::BadArgument;
        i2c_writes.push_back({static_cast<unsigned int>(data[0]),
                               static_cast<unsigned int>(data[1])});
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_configure(
        const mm::mcu::I2sConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.baud == 0) return mm::mcu::Status::BadArgument;
        i2s_configured = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_write(unsigned int, std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2s_configured || data.empty()) return mm::mcu::Status::BadArgument;
        i2s_written.insert(i2s_written.end(), data.begin(), data.end());
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += milliseconds;
        return mm::mcu::Status::Ok;
    }

    bool i2c_configured = false;
    bool i2s_configured = false;
    std::vector<std::pair<unsigned int, unsigned int>> i2c_writes;
    std::vector<std::byte> i2s_written;
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
void mm_test_es8311_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_es8311_force(mm::mcu::Status status) { platform.forced = status; }
bool mm_test_es8311_i2c_configured() { return platform.i2c_configured; }
bool mm_test_es8311_i2s_configured() { return platform.i2s_configured; }
std::size_t mm_test_es8311_write_count() { return platform.i2c_writes.size(); }
unsigned int mm_test_es8311_write(std::size_t index) {
    return index < platform.i2c_writes.size()
             ? platform.i2c_writes[index].first + platform.i2c_writes[index].second * 0x100
             : 0;
}
unsigned long mm_test_es8311_ticks() { return platform.ticks; }
std::size_t mm_test_es8311_i2s_size() { return platform.i2s_written.size(); }
unsigned int mm_test_es8311_i2s_byte(std::size_t index) {
    return index < platform.i2s_written.size()
             ? static_cast<unsigned int>(platform.i2s_written[index])
             : 0;
}
