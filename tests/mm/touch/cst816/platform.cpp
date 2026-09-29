// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as a CST816 does. It is a stand-in
// for the bus and the controller together: the driver's correctness is what
// the bytes it sends and the bytes it makes of the reply, and both are
// observable here without a panel.
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

import mm.mcu;

namespace {

constexpr unsigned int device_address = 0x15;
constexpr std::size_t point_size = 4;  // the coordinate pair the controller holds

class RecordingPlatform : public mm::mcu::Platform {
public:
    RecordingPlatform() { reset(); }

    void reset() {
        chip_id = 0xb6;
        finger_count = 0;
        coordinate.assign(point_size, std::byte{0});
        configured = false;
        writes.clear();
        reads.clear();
        gpio_configured.clear();
        gpio_writes.clear();
        ticks = 0;
        forced = mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        gpio_configured.push_back({pin, direction == mm::mcu::Direction::Out});
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        gpio_writes.push_back({pin, high});
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.baud == 0) return mm::mcu::Status::BadArgument;
        configured = true;
        return mm::mcu::Status::Ok;
    }

    // A write is a register byte and its value, the only kind of write this
    // controller takes.
    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || address != device_address || data.size() != 2)
            return mm::mcu::Status::BadArgument;
        writes.push_back({static_cast<unsigned int>(data[0]), static_cast<unsigned int>(data[1])});
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || address != device_address || command.size() != 1 || data.empty())
            return mm::mcu::Status::BadArgument;
        const auto reg = static_cast<unsigned int>(command[0]);
        reads.push_back(reg);
        answer(reg, data);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += milliseconds;
        return mm::mcu::Status::Ok;
    }

    void answer(unsigned int reg, std::span<std::byte> data) {
        for (auto& byte : data) byte = std::byte{0};
        if (reg == 0xa7 && !data.empty()) {
            data[0] = static_cast<std::byte>(chip_id);
            return;
        }
        if (reg == 0x02 && !data.empty()) {
            data[0] = static_cast<std::byte>(finger_count & 0x0f);
            return;
        }
        if (reg == 0x03) {
            for (std::size_t i = 0; i < data.size() && i < coordinate.size(); ++i)
                data[i] = coordinate[i];
            return;
        }
    }

    unsigned int chip_id;
    unsigned int finger_count;
    std::vector<std::byte> coordinate;
    bool configured = false;
    std::vector<std::pair<unsigned int, unsigned int>> writes;
    std::vector<unsigned int> reads;
    std::vector<std::pair<unsigned int, bool>> gpio_configured;
    std::vector<std::pair<unsigned int, bool>> gpio_writes;
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
void mm_test_touch816_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_touch816_force(mm::mcu::Status status) { platform.forced = status; }
void mm_test_touch816_chip_id(unsigned int value) { platform.chip_id = value; }
void mm_test_touch816_fingers(unsigned int value) { platform.finger_count = value; }
void mm_test_touch816_coordinate(unsigned int x_high, unsigned int x_low, unsigned int y_high,
                                 unsigned int y_low) {
    platform.coordinate[0] = static_cast<std::byte>(x_high);
    platform.coordinate[1] = static_cast<std::byte>(x_low);
    platform.coordinate[2] = static_cast<std::byte>(y_high);
    platform.coordinate[3] = static_cast<std::byte>(y_low);
}
std::size_t mm_test_touch816_write_count() { return platform.writes.size(); }
unsigned int mm_test_touch816_write(std::size_t index) {
    return index < platform.writes.size()
             ? platform.writes[index].first + platform.writes[index].second * 0x100
             : 0;
}
std::size_t mm_test_touch816_read_count() { return platform.reads.size(); }
unsigned int mm_test_touch816_read(std::size_t index) {
    return index < platform.reads.size() ? platform.reads[index] : 0;
}
unsigned long mm_test_touch816_ticks() { return platform.ticks; }
std::size_t mm_test_touch816_gpio_writes() { return platform.gpio_writes.size(); }
bool mm_test_touch816_gpio_level(std::size_t index) {
    return index < platform.gpio_writes.size() ? platform.gpio_writes[index].second : false;
}
