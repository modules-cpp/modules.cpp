// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform for an SPI display: the transcript is what the
// controller said and whether it said it as a command or as data, which is the
// whole of a display driver's observable behaviour.
#include <cstddef>
#include <span>
#include <vector>

import mm.mcu;

namespace {

struct Transfer {
    bool data = false;
    std::vector<std::byte> bytes;
};

class RecordingPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int, mm::mcu::Direction,
                                                 mm::mcu::Pull) override {
        return forced;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        gpio_writes.push_back({pin, high});
        if (pin == data_command_pin) data_mode = high;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.baud == 0) return mm::mcu::Status::BadArgument;
        configured = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_write(unsigned int,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!configured) return mm::mcu::Status::BadArgument;
        transcript.push_back({data_mode, {data.begin(), data.end()}});
        if (data.size() > largest) largest = data.size();
        // Command parameters travel in data mode too, so pixels are only the
        // data that follows a memory-write command.
        if (!data_mode && data.size() == 1)
            in_pixels = static_cast<unsigned int>(data.front()) == 0x2c;
        else if (data_mode && in_pixels)
            pixel_bytes += data.size();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long value) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += value;
        return mm::mcu::Status::Ok;
    }

    void reset() { *this = RecordingPlatform{}; }

    unsigned int data_command_pin = 8;
    bool data_mode = false;
    bool configured = false;
    bool in_pixels = false;
    std::vector<Transfer> transcript;
    std::vector<std::pair<unsigned int, bool>> gpio_writes;
    std::size_t largest = 0;
    std::size_t pixel_bytes = 0;
    unsigned long ticks = 0;
    mm::mcu::Status forced = mm::mcu::Status::Ok;
};

RecordingPlatform platform;

struct Register {
    Register() { mm::mcu::set_platform(platform); }
};

const Register registered;

}  // namespace

void mm_test_lcd_reset() { platform.reset(); }
void mm_test_lcd_force(mm::mcu::Status status) { platform.forced = status; }
std::size_t mm_test_lcd_transcript_size() { return platform.transcript.size(); }
bool mm_test_lcd_is_data(std::size_t index) {
    return index < platform.transcript.size() && platform.transcript[index].data;
}
std::size_t mm_test_lcd_transfer_size(std::size_t index) {
    return index < platform.transcript.size() ? platform.transcript[index].bytes.size() : 0;
}
unsigned int mm_test_lcd_byte(std::size_t index, std::size_t offset) {
    if (index >= platform.transcript.size()) return 0x100;
    const auto& bytes = platform.transcript[index].bytes;
    return offset < bytes.size() ? static_cast<unsigned int>(bytes[offset]) : 0x100;
}
// The index of the nth command transfer carrying a given opcode, or the
// transcript size when it never appeared.
std::size_t mm_test_lcd_find_command(unsigned int opcode) {
    for (std::size_t index = 0; index < platform.transcript.size(); ++index) {
        const auto& entry = platform.transcript[index];
        if (!entry.data && entry.bytes.size() == 1 &&
            static_cast<unsigned int>(entry.bytes.front()) == opcode)
            return index;
    }
    return platform.transcript.size();
}
std::size_t mm_test_lcd_largest_transfer() { return platform.largest; }
std::size_t mm_test_lcd_pixel_bytes() { return platform.pixel_bytes; }
unsigned long mm_test_lcd_ticks() { return platform.ticks; }
std::size_t mm_test_lcd_gpio_writes() { return platform.gpio_writes.size(); }
unsigned int mm_test_lcd_gpio_pin(std::size_t index) {
    return index < platform.gpio_writes.size() ? platform.gpio_writes[index].first : 0;
}
bool mm_test_lcd_gpio_level(std::size_t index) {
    return index < platform.gpio_writes.size() ? platform.gpio_writes[index].second : false;
}
