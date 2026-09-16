// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

import mm.mcu;

namespace {

class RecordingPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status gpio_configure(
        unsigned int, mm::mcu::Direction, mm::mcu::Pull) override {
        return forced;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (forced == mm::mcu::Status::Ok) {
            gpio_writes.push_back({pin, high});
            if (pin == 5) data_mode = high;
        }
        return forced;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int, bool& high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        high = busy;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& value) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        configured = value.baud != 0;
        return configured ? mm::mcu::Status::Ok : mm::mcu::Status::BadArgument;
    }

    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int, std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        bytes.insert(bytes.end(), data.begin(), data.end());
        transcript.push_back({data_mode, {data.begin(), data.end()}});
        ++writes;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long value) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += value;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& value) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        value = ticks;
        return mm::mcu::Status::Ok;
    }

    void reset() {
        configured = false;
        busy = false;
        forced = mm::mcu::Status::Ok;
        ticks = 0;
        writes = 0;
        data_mode = false;
        bytes.clear();
        transcript.clear();
        gpio_writes.clear();
    }

    struct Transfer {
        bool data;
        std::vector<std::byte> bytes;
    };

    bool configured = false;
    bool busy = false;
    mm::mcu::Status forced = mm::mcu::Status::Ok;
    unsigned long ticks = 0;
    unsigned int writes = 0;
    bool data_mode = false;
    std::vector<std::byte> bytes;
    std::vector<Transfer> transcript;
    std::vector<std::pair<unsigned int, bool>> gpio_writes;
};

RecordingPlatform platform;

struct Register {
    Register() { mm::mcu::set_platform(platform); }
};

const Register registered;

}

// A configured board may inject its own mm.mcu platform into this binary,
// and its static registration may run after ours. Reclaim the seam so every
// case deterministically exercises the controller through this fake.
void mm_test_epaper_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_epaper_busy(bool busy) { platform.busy = busy; }
void mm_test_epaper_force(mm::mcu::Status status) { platform.forced = status; }
bool mm_test_epaper_contains(unsigned int value) {
    for (const auto byte : platform.bytes)
        if (static_cast<unsigned int>(byte) == value) return true;
    return false;
}
unsigned int mm_test_epaper_writes() { return platform.writes; }
std::size_t mm_test_epaper_transcript_size() { return platform.transcript.size(); }
bool mm_test_epaper_transfer_is_data(std::size_t index) {
    return index < platform.transcript.size() && platform.transcript[index].data;
}
std::size_t mm_test_epaper_transfer_size(std::size_t index) {
    return index < platform.transcript.size() ? platform.transcript[index].bytes.size() : 0;
}
unsigned int mm_test_epaper_transfer_byte(std::size_t index, std::size_t byte) {
    return index < platform.transcript.size() &&
                   byte < platform.transcript[index].bytes.size()
               ? static_cast<unsigned int>(platform.transcript[index].bytes[byte])
               : 0;
}
unsigned int mm_test_epaper_command_count(unsigned int command) {
    unsigned int count = 0;
    for (const auto& transfer : platform.transcript)
        if (!transfer.data && transfer.bytes.size() == 1 &&
            static_cast<unsigned int>(transfer.bytes.front()) == command)
            ++count;
    return count;
}
std::size_t mm_test_epaper_largest_transfer() {
    std::size_t largest = 0;
    for (const auto& transfer : platform.transcript)
        if (transfer.bytes.size() > largest) largest = transfer.bytes.size();
    return largest;
}
std::size_t mm_test_epaper_gpio_write_count(unsigned int pin) {
    std::size_t count = 0;
    for (const auto& write : platform.gpio_writes)
        if (write.first == pin) ++count;
    return count;
}
bool mm_test_epaper_gpio_write_level(unsigned int pin, std::size_t index) {
    std::size_t current = 0;
    for (const auto& write : platform.gpio_writes) {
        if (write.first != pin) continue;
        if (current == index) return write.second;
        ++current;
    }
    return false;
}
