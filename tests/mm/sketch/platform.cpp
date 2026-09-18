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

class TestPlatform : public mm::mcu::Platform {
public:
    bool configured[pin_count] = {};
    bool output[pin_count] = {};
    bool level[pin_count] = {};
    unsigned long clock_ticks = 1000;

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
