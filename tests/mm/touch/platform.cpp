// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as a CST328 does. It is a stand-in for
// the bus and the controller together: the driver's correctness is what the
// bytes it sends and the bytes it makes of the reply, and both are observable
// here without a panel.
#include <cstddef>
#include <span>
#include <vector>

import mm.mcu;

namespace {

constexpr unsigned int device_address = 0x1a;

class RecordingPlatform : public mm::mcu::Platform {
public:
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

    // A write with no payload beyond the two address bytes is a mode selection,
    // which is the only kind of write this controller takes.
    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || address != device_address || data.size() != 2)
            return mm::mcu::Status::BadArgument;
        commands.push_back(register_of(data));
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured || address != device_address || command.size() != 2 || data.empty())
            return mm::mcu::Status::BadArgument;
        const auto reg = register_of(command);
        reads.push_back(reg);
        ++transactions;
        answer(reg, data);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long value) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += value;
        return mm::mcu::Status::Ok;
    }

    void reset() {
        *this = RecordingPlatform{};
    }

    [[nodiscard]] static unsigned int register_of(std::span<const std::byte> bytes) {
        return (static_cast<unsigned int>(bytes[0]) << 8) |
               static_cast<unsigned int>(bytes[1]);
    }

    void answer(unsigned int reg, std::span<std::byte> data) {
        for (auto& byte : data) byte = std::byte{0};
        if (reg == 0xd1f4) {
            // The identification block, with the check code at offset ten.
            if (data.size() > 11) {
                data[10] = static_cast<std::byte>(check_code & 0xff);
                data[11] = static_cast<std::byte>((check_code >> 8) & 0xff);
            }
            return;
        }
        if (reg == 0xd005) {
            if (!data.empty()) data[0] = static_cast<std::byte>(reported_points & 0x0f);
            return;
        }
        if (reg == 0xd000) {
            if (!report.empty())
                for (std::size_t i = 0; i < data.size() && i < report.size(); ++i)
                    data[i] = report[i];
            return;
        }
    }

    bool configured = false;
    unsigned int check_code = 0xcaca;
    unsigned int reported_points = 0;
    std::vector<std::byte> report;
    std::vector<unsigned int> commands;
    std::vector<unsigned int> reads;
    std::vector<std::pair<unsigned int, bool>> gpio_configured;
    std::vector<std::pair<unsigned int, bool>> gpio_writes;
    std::size_t transactions = 0;
    unsigned long ticks = 0;
    mm::mcu::Status forced = mm::mcu::Status::Ok;
};

RecordingPlatform platform;

struct Register {
    Register() { mm::mcu::set_platform(platform); }
};

const Register registered;

}  // namespace

void mm_test_touch_reset() { platform.reset(); }
void mm_test_touch_force(mm::mcu::Status status) { platform.forced = status; }
void mm_test_touch_check_code(unsigned int value) { platform.check_code = value; }
void mm_test_touch_points(unsigned int value) { platform.reported_points = value; }
void mm_test_touch_report(const unsigned char* bytes, std::size_t size) {
    platform.report.clear();
    platform.report.reserve(size);
    for (std::size_t index = 0; index < size; ++index)
        platform.report.push_back(static_cast<std::byte>(bytes[index]));
}
std::size_t mm_test_touch_command_count() { return platform.commands.size(); }
unsigned int mm_test_touch_command(std::size_t index) {
    return index < platform.commands.size() ? platform.commands[index] : 0;
}
std::size_t mm_test_touch_read_count() { return platform.reads.size(); }
unsigned int mm_test_touch_read(std::size_t index) {
    return index < platform.reads.size() ? platform.reads[index] : 0;
}
std::size_t mm_test_touch_transactions() { return platform.transactions; }
unsigned long mm_test_touch_ticks() { return platform.ticks; }
std::size_t mm_test_touch_gpio_writes() { return platform.gpio_writes.size(); }
bool mm_test_touch_gpio_level(std::size_t index) {
    return index < platform.gpio_writes.size() ? platform.gpio_writes[index].second : false;
}
