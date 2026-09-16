// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as a QMI8658 does. The part's registers
// are a flat array here, which is enough: the driver's correctness is the
// addresses it touches, the values it writes, and what it makes of the reply.
#include <array>
#include <cstddef>
#include <span>
#include <vector>

import mm.mcu;

namespace {

constexpr unsigned int register_count = 64;

class RecordingPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.baud == 0) return mm::mcu::Status::BadArgument;
        configured = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || address != present_address || data.size() != 2)
            return mm::mcu::Status::BadArgument;
        const auto reg = static_cast<unsigned int>(data[0]);
        const auto value = static_cast<unsigned int>(data[1]);
        writes.push_back({reg, value});
        if (reg < register_count) registers[reg] = static_cast<unsigned char>(value);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || command.size() != 1 || data.empty())
            return mm::mcu::Status::BadArgument;
        // An address nothing is strapped to does not acknowledge, which is what
        // the driver's two-address search has to survive.
        if (address != present_address) return mm::mcu::Status::BadArgument;
        const auto reg = static_cast<unsigned int>(command[0]);
        reads.push_back(reg);
        ++transactions;
        for (std::size_t index = 0; index < data.size(); ++index) {
            const auto source = reg + index;
            data[index] = source < register_count
                              ? static_cast<std::byte>(registers[source])
                              : std::byte{0};
        }
        return mm::mcu::Status::Ok;
    }

    void reset() {
        *this = RecordingPlatform{};
        registers[0] = 0x05;  // WhoAmI
        registers[1] = 0x7a;  // revision
    }

    std::array<unsigned char, register_count> registers{};
    unsigned int present_address = 0x6b;
    bool configured = false;
    std::vector<std::pair<unsigned int, unsigned int>> writes;
    std::vector<unsigned int> reads;
    std::size_t transactions = 0;
    mm::mcu::Status forced = mm::mcu::Status::Ok;
};

RecordingPlatform platform;

struct Register {
    Register() {
        platform.reset();
        mm::mcu::set_platform(platform);
    }
};

const Register registered;

}  // namespace

// A configured board may inject its own mm.mcu platform into this binary,
// and its static registration may run after ours. Reclaim the seam so every
// case deterministically exercises the driver through this fake.
void mm_test_imu_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_imu_force(mm::mcu::Status status) { platform.forced = status; }
void mm_test_imu_address(unsigned int address) { platform.present_address = address; }
void mm_test_imu_identifier(unsigned int value) {
    platform.registers[0] = static_cast<unsigned char>(value);
}
void mm_test_imu_set_register(unsigned int reg, unsigned int value) {
    if (reg < register_count) platform.registers[reg] = static_cast<unsigned char>(value);
}
std::size_t mm_test_imu_write_count() { return platform.writes.size(); }
unsigned int mm_test_imu_write_register(std::size_t index) {
    return index < platform.writes.size() ? platform.writes[index].first : 0;
}
unsigned int mm_test_imu_write_value(std::size_t index) {
    return index < platform.writes.size() ? platform.writes[index].second : 0;
}
std::size_t mm_test_imu_read_count() { return platform.reads.size(); }
unsigned int mm_test_imu_read(std::size_t index) {
    return index < platform.reads.size() ? platform.reads[index] : 0;
}
std::size_t mm_test_imu_transactions() { return platform.transactions; }
