// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as a PCF85063 does: a flat register
// file, which is all this part is from the bus's point of view.
#include <array>
#include <cstddef>
#include <span>

import mm.mcu;

namespace {

constexpr unsigned int register_count = 32;
constexpr unsigned int device_address = 0x51;

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
        if (!configured || address != device_address || data.size() < 2)
            return mm::mcu::Status::BadArgument;
        ++writes;
        written = data.size() - 1;
        auto reg = static_cast<unsigned int>(data[0]);
        for (std::size_t index = 1; index < data.size(); ++index, ++reg)
            if (reg < register_count)
                registers[reg] = static_cast<unsigned char>(data[index]);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || address != device_address || command.size() != 1 || data.empty())
            return mm::mcu::Status::BadArgument;
        ++transactions;
        const auto reg = static_cast<unsigned int>(command[0]);
        for (std::size_t index = 0; index < data.size(); ++index) {
            const auto source = reg + index;
            data[index] = source < register_count
                              ? static_cast<std::byte>(registers[source])
                              : std::byte{0};
        }
        return mm::mcu::Status::Ok;
    }

    void reset() { *this = RecordingPlatform{}; }

    std::array<unsigned char, register_count> registers{};
    bool configured = false;
    std::size_t transactions = 0;
    std::size_t writes = 0;
    std::size_t written = 0;
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
void mm_test_rtc_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_rtc_force(mm::mcu::Status status) { platform.forced = status; }
void mm_test_rtc_set_register(unsigned int reg, unsigned int value) {
    if (reg < register_count) platform.registers[reg] = static_cast<unsigned char>(value);
}
unsigned int mm_test_rtc_register(unsigned int reg) {
    return reg < register_count ? platform.registers[reg] : 0;
}
std::size_t mm_test_rtc_transactions() { return platform.transactions; }
std::size_t mm_test_rtc_writes() { return platform.writes; }
std::size_t mm_test_rtc_written_bytes() { return platform.written; }
