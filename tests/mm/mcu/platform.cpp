// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A stand-in platform for mm.mcu, and a demonstration of the supply mechanism
// rather than a mock of it: it registers a Platform subclass exactly as a real
// platform's module does, and nothing here is scaffolding a real platform would
// not also have to provide.
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

import mm.mcu;

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

class Stand : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Board board() const override {
        return {"stand", gpios, mm::mcu::Led{"status", 25, false}};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (pull != mm::mcu::Pull::None && pull != mm::mcu::Pull::Up &&
            pull != mm::mcu::Pull::Down)
            return mm::mcu::Status::BadArgument;
        configured[pin] = true;
        output[pin] = direction == mm::mcu::Direction::Out;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (!configured[pin]) return mm::mcu::Status::BadArgument;
        if (!output[pin]) return mm::mcu::Status::Unsupported;
        level[pin] = high;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count || !configured[pin]) return mm::mcu::Status::BadArgument;
        high = level[pin];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.instance > 1 || configuration.baud == 0 ||
            configuration.clock_gpio >= pin_count ||
            configuration.transmit_gpio >= pin_count ||
            configuration.clock_gpio == configuration.transmit_gpio ||
            (configuration.receive_gpio && *configuration.receive_gpio >= pin_count))
            return mm::mcu::Status::BadArgument;
        if (configuration.receive_gpio &&
            (*configuration.receive_gpio == configuration.clock_gpio ||
             *configuration.receive_gpio == configuration.transmit_gpio))
            return mm::mcu::Status::BadArgument;
        spi_configuration = configuration;
        spi_ready = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int instance, std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!spi_ready || instance != spi_configuration.instance)
            return mm::mcu::Status::BadArgument;
        spi_written.insert(spi_written.end(), data.begin(), data.end());
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_transfer(
        unsigned int instance, std::span<const std::byte> transmit,
        std::span<std::byte> receive) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!spi_ready || instance != spi_configuration.instance ||
            transmit.size() != receive.size())
            return mm::mcu::Status::BadArgument;
        spi_written.insert(spi_written.end(), transmit.begin(), transmit.end());
        for (std::size_t i = 0; i < transmit.size(); ++i)
            receive[i] = transmit[i];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.instance > 1 || configuration.baud == 0 ||
            configuration.data_gpio >= pin_count ||
            configuration.clock_gpio >= pin_count ||
            configuration.data_gpio == configuration.clock_gpio)
            return mm::mcu::Status::BadArgument;
        i2c_configuration = configuration;
        i2c_ready = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int instance, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2c_bus_ready(instance, address) || data.empty())
            return mm::mcu::Status::BadArgument;
        i2c_written.insert(i2c_written.end(), data.begin(), data.end());
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_read(unsigned int instance, unsigned int address,
                                           std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2c_bus_ready(instance, address) || data.empty())
            return mm::mcu::Status::BadArgument;
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = i2c_next_byte();
        return mm::mcu::Status::Ok;
    }

    // Recorded as one transaction: the command bytes land in the same
    // transcript as a write, and the read that follows is not separable from
    // it, which is what the interface promises.
    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int instance, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2c_bus_ready(instance, address) || command.empty() || data.empty())
            return mm::mcu::Status::BadArgument;
        i2c_written.insert(i2c_written.end(), command.begin(), command.end());
        ++i2c_write_reads;
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = i2c_next_byte();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status uart_write(unsigned int instance, const char* text) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (text == nullptr) return mm::mcu::Status::BadArgument;
        if (instance != 0) return mm::mcu::Status::Unsupported;
        uart_instance = instance;
        uart_text = text;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += milliseconds;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& out) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        out = ticks;
        return mm::mcu::Status::Ok;
    }

    void reset() {
        for (unsigned int pin = 0; pin < pin_count; ++pin) {
            configured[pin] = false;
            output[pin] = false;
            level[pin] = false;
        }
        forced = mm::mcu::Status::Ok;
        ticks = 0;
        uart_instance = 0;
        uart_text = nullptr;
        spi_ready = false;
        spi_configuration = {};
        spi_written.clear();
        i2c_ready = false;
        i2c_configuration = {};
        i2c_written.clear();
        i2c_write_reads = 0;
        i2c_reply = 0;
    }

    // Seven-bit addressing, and one device on the bus. A driver aimed at any
    // other address gets the same BadArgument a real bus reports as a missing
    // acknowledgement.
    [[nodiscard]] bool i2c_bus_ready(unsigned int instance, unsigned int address) const {
        return i2c_ready && instance == i2c_configuration.instance &&
               address == i2c_device_address;
    }

    [[nodiscard]] std::byte i2c_next_byte() {
        return static_cast<std::byte>(i2c_reply++);
    }

    bool configured[pin_count] = {};
    bool output[pin_count] = {};
    bool level[pin_count] = {};
    mm::mcu::Status forced = mm::mcu::Status::Ok;
    unsigned long ticks = 0;
    unsigned int uart_instance = 0;
    const char* uart_text = nullptr;
    bool spi_ready = false;
    mm::mcu::SpiConfiguration spi_configuration;
    std::vector<std::byte> spi_written;
    static constexpr unsigned int i2c_device_address = 0x1a;
    bool i2c_ready = false;
    mm::mcu::I2cConfiguration i2c_configuration;
    std::vector<std::byte> i2c_written;
    std::size_t i2c_write_reads = 0;
    unsigned int i2c_reply = 0;
};

Stand stand;

// Registration at static initialisation, the way a platform module does it. The
// object is linked because objects are linked directly rather than through an
// archive, so nothing has to reference it for it to arrive.
struct Register {
    Register() { mm::mcu::set_platform(stand); }
};

const Register registered;

}  // namespace

// The control surface the test uses, as free functions so the Stand type itself
// stays internal. A real platform has no such surface, and mm.mcu cannot see it:
// none of it is part of the Platform interface.
// A configured board may inject its own mm.mcu platform into this binary,
// and its static registration may run after ours. Reclaim the seam so every
// case deterministically exercises the portable interface through this stand.
void mm_test_reset() {
    mm::mcu::set_platform(stand);
    stand.reset();
}
void mm_test_force(mm::mcu::Status status) { stand.forced = status; }
void mm_test_set_level(unsigned int pin, bool high) {
    if (pin < pin_count) stand.level[pin] = high;
}
unsigned long mm_test_ticks() { return stand.ticks; }
unsigned int mm_test_uart_instance() { return stand.uart_instance; }
bool mm_test_uart_written() { return stand.uart_text != nullptr; }
bool mm_test_spi_ready() { return stand.spi_ready; }
unsigned long mm_test_spi_baud() { return stand.spi_configuration.baud; }
std::size_t mm_test_spi_size() { return stand.spi_written.size(); }
unsigned int mm_test_spi_byte(std::size_t index) {
    return index < stand.spi_written.size()
               ? static_cast<unsigned int>(stand.spi_written[index])
               : 0;
}
bool mm_test_i2c_ready() { return stand.i2c_ready; }
unsigned long mm_test_i2c_baud() { return stand.i2c_configuration.baud; }
unsigned int mm_test_i2c_address() { return Stand::i2c_device_address; }
std::size_t mm_test_i2c_size() { return stand.i2c_written.size(); }
unsigned int mm_test_i2c_byte(std::size_t index) {
    return index < stand.i2c_written.size()
               ? static_cast<unsigned int>(stand.i2c_written[index])
               : 0;
}
std::size_t mm_test_i2c_write_reads() { return stand.i2c_write_reads; }
