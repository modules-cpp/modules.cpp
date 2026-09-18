// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

import mm.mcu;
import mm.stdio;

namespace {

constexpr unsigned int pin_count = 32;

constexpr mm::mcu::Gpio gpios[] = {
    {0, "GPIO0"},   {1, "GPIO1"},   {2, "GPIO2"},   {3, "GPIO3"},
    {4, "GPIO4"},   {5, "GPIO5"},   {6, "GPIO6"},   {7, "GPIO7"},
    {8, "GPIO8"},   {9, "GPIO9"},   {10, "GPIO10"}, {11, "GPIO11"},
    {12, "GPIO12"}, {13, "GPIO13"}, {14, "GPIO14"}, {15, "GPIO15"},
    {16, "GPIO16"}, {17, "GPIO17"}, {18, "GPIO18"}, {19, "GPIO19"},
    {20, "GPIO20"}, {21, "GPIO21"}, {22, "GPIO22"}, {23, "GPIO23"},
    {24, "GPIO24"}, {25, "GPIO25"}, {26, "GPIO26"}, {27, "GPIO27"},
    {28, "GPIO28"}, {29, "GPIO29"}, {30, "GPIO30"}, {31, "GPIO31"},
};

struct GpioEvent {
    unsigned int pin;
    bool high;
};

class TestPlatform : public mm::mcu::Platform {
public:
    bool configured[pin_count] = {};
    bool output[pin_count] = {};
    bool level[pin_count] = {};
    unsigned long clock_ticks = 1000;

    bool log_enabled = false;
    std::vector<GpioEvent> write_log;

    bool feed_shift_in = false;
    unsigned int feed_clock_pin = 0;
    unsigned int feed_data_pin = 0;
    unsigned char feed_byte = 0;
    bool feed_lsb_first = true;
    unsigned int feed_bit_index = 0;

    [[nodiscard]] mm::mcu::Board board() const override {
        return {"test-board", gpios, mm::mcu::Led{"status", 25, true}};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        configured[pin] = true;
        output[pin] = direction == mm::mcu::Direction::Out;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (pin >= pin_count || !configured[pin]) return mm::mcu::Status::BadArgument;
        if (!output[pin]) return mm::mcu::Status::Unsupported;
        level[pin] = high;
        if (log_enabled) {
            write_log.push_back({pin, high});
        }
        if (feed_shift_in && pin == feed_clock_pin && high) {
            if (feed_bit_index < 8) {
                bool bit = false;
                if (feed_lsb_first) {
                    bit = ((feed_byte & (1u << feed_bit_index)) != 0);
                } else {
                    bit = ((feed_byte & (1u << (7 - feed_bit_index))) != 0);
                }
                level[feed_data_pin] = bit;
                ++feed_bit_index;
            }
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (pin >= pin_count || !configured[pin]) return mm::mcu::Status::BadArgument;
        high = level[pin];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long ms) override {
        clock_ticks += ms;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = clock_ticks;
        return mm::mcu::Status::Ok;
    }
};

class TestConsole : public mm::stdio::Console {
public:
    bool initialized = false;
    std::vector<std::byte> written_data;
    std::vector<std::byte> pending_read;
    std::size_t read_offset = 0;
    int read_calls = 0;
    int flushes = 0;

    [[nodiscard]] mm::stdio::Status initialize() override {
        initialized = true;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status write(std::span<const std::byte> data,
                                          std::size_t& written) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        written_data.insert(written_data.end(), data.begin(), data.end());
        written = data.size();
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status read(std::span<std::byte> data,
                                         std::size_t& count) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        const auto available = pending_read.size() - read_offset;
        const auto taken = data.size() < available ? data.size() : available;
        for (std::size_t i = 0; i < taken; ++i) {
            data[i] = pending_read[read_offset + i];
        }
        read_offset += taken;
        count = taken;
        ++read_calls;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status flush() override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        ++flushes;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status connected(bool& value) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        value = true;
        return mm::stdio::Status::Ok;
    }
};

TestPlatform platform_instance;
TestConsole console_instance;

struct Init {
    Init() {
        mm::mcu::set_platform(platform_instance);
        mm::stdio::set_console(console_instance);
    }
} init_instance;

} // namespace

int test_console_read_calls() {
    return console_instance.read_calls;
}

void reset_test_console_read_calls() {
    console_instance.read_calls = 0;
}

void test_start_gpio_log() {
    platform_instance.write_log.clear();
    platform_instance.log_enabled = true;
}

std::size_t test_gpio_log_size() {
    return platform_instance.write_log.size();
}

unsigned int test_gpio_log_pin(std::size_t idx) {
    return platform_instance.write_log[idx].pin;
}

bool test_gpio_log_high(std::size_t idx) {
    return platform_instance.write_log[idx].high;
}

void test_stop_gpio_log() {
    platform_instance.log_enabled = false;
}

void test_setup_shift_in(unsigned int data_pin, unsigned int clock_pin, unsigned char val, bool lsb_first) {
    platform_instance.configured[data_pin] = true;
    platform_instance.output[data_pin] = false;
    platform_instance.level[data_pin] = false;
    platform_instance.feed_shift_in = true;
    platform_instance.feed_clock_pin = clock_pin;
    platform_instance.feed_data_pin = data_pin;
    platform_instance.feed_byte = val;
    platform_instance.feed_lsb_first = lsb_first;
    platform_instance.feed_bit_index = 0;
}

void test_clear_shift_in() {
    platform_instance.feed_shift_in = false;
}

