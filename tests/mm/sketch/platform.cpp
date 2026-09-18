// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

constexpr mm::mcu::AdcChannel test_adc_channels[] = {
    {0, "ADC0", 26, 12, 3300},
    {1, "ADC1", 27, 10, 3300},
    {2, "ADC2", 28, 12, 0},
    {3, "TEMP", std::nullopt, 12, 3300},
};

constexpr std::uint64_t pwm_shortest = 16;
constexpr std::uint64_t pwm_longest = 134'000'000;

constexpr mm::mcu::PwmOutput test_pwm_outputs[] = {
    {0, "PWM0", 0, 0, 0, pwm_shortest, pwm_longest},
    {1, "PWM1", 1, 0, 1, pwm_shortest, pwm_longest},
    {2, "PWM2", 2, 1, 0, pwm_shortest, pwm_longest},
    {3, "PWM3", 3, 2, 0, 0, 0},
    {9, "PWM9", 25, 4, 1, pwm_shortest, pwm_longest},
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
    bool spi_present = true;
    bool i2c_present = true;

    [[nodiscard]] mm::mcu::Board board() const override {
        std::optional<mm::mcu::SpiWiring> spi_wiring;
        std::optional<mm::mcu::I2cWiring> i2c_wiring;
        if (spi_present) {
            spi_wiring = mm::mcu::SpiWiring{0, 18, 19, 16};
        }
        if (i2c_present) {
            i2c_wiring = mm::mcu::I2cWiring{0, 4, 5};
        }
        return {"test-board", gpios, mm::mcu::Led{"status", 25, true},
                spi_wiring, i2c_wiring};
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

    bool feed_pulse = false;
    unsigned int feed_pulse_pin = 0;
    std::vector<bool> feed_pulse_levels;
    std::size_t feed_pulse_index = 0;
    unsigned long pulse_step_us = 100;

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (pin >= pin_count || !configured[pin]) return mm::mcu::Status::BadArgument;
        if (feed_pulse && pin == feed_pulse_pin && feed_pulse_index < feed_pulse_levels.size()) {
            level[pin] = feed_pulse_levels[feed_pulse_index++];
            clock_ticks_us += pulse_step_us;
        }
        high = level[pin];
        return mm::mcu::Status::Ok;
    }

    bool watched[pin_count] = {};
    bool pending_edge[pin_count] = {};
    mm::mcu::Pull watch_pull[pin_count] = {};
    mm::mcu::Edge watch_edge[pin_count] = {};

    [[nodiscard]] mm::mcu::Status gpio_watch(unsigned int pin, mm::mcu::Pull pull,
                                             mm::mcu::Edge edge) override {
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (watched[pin]) return mm::mcu::Status::Busy;
        watched[pin] = true;
        watch_pull[pin] = pull;
        watch_edge[pin] = edge;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_unwatch(unsigned int pin) override {
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        watched[pin] = false;
        pending_edge[pin] = false;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_take(unsigned int pin, bool& pending) override {
        if (pin >= pin_count || !watched[pin]) return mm::mcu::Status::BadArgument;
        pending = pending_edge[pin];
        pending_edge[pin] = false;
        return mm::mcu::Status::Ok;
    }

    bool ticks_fail = false;
    bool delay_fail = false;
    unsigned long clock_ticks_us = 1'000'000;

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long ms) override {
        if (delay_fail) return mm::mcu::Status::TransportError;
        clock_ticks += ms;
        clock_ticks_us += ms * 1000UL;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        if (ticks_fail) return mm::mcu::Status::Unsupported;
        ticks = clock_ticks;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_us(unsigned long us) override {
        if (delay_fail) return mm::mcu::Status::TransportError;
        clock_ticks_us += us;
        clock_ticks += us / 1000UL;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_us(unsigned long& ticks) override {
        if (ticks_fail) return mm::mcu::Status::Unsupported;
        ticks = clock_ticks_us;
        return mm::mcu::Status::Ok;
    }

    // SPI mock: loopback (rx = tx ^ spi_xor_mask)
    bool spi_configured = false;
    unsigned char spi_xor_mask = 0;
    bool spi_fail = false;
    mm::mcu::SpiConfiguration last_spi_config{};

    [[nodiscard]] mm::mcu::Status spi_configure(const mm::mcu::SpiConfiguration& config) override {
        if (spi_fail) return mm::mcu::Status::TransportError;
        last_spi_config = config;
        spi_configured = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_transfer(unsigned int /*instance*/,
                                               std::span<const std::byte> transmit,
                                               std::span<std::byte> receive) override {
        if (spi_fail) return mm::mcu::Status::TransportError;
        if (!spi_configured) return mm::mcu::Status::BadArgument;
        if (transmit.size() != receive.size()) return mm::mcu::Status::BadArgument;
        for (std::size_t i = 0; i < transmit.size(); ++i) {
            receive[i] = static_cast<std::byte>(
                static_cast<unsigned char>(transmit[i]) ^ spi_xor_mask);
        }
        return mm::mcu::Status::Ok;
    }

    // I2C mock: buffer-based
    bool i2c_configured = false;
    bool i2c_fail = false;
    std::vector<std::byte> i2c_written;
    unsigned int i2c_written_address = 0;
    std::vector<std::byte> i2c_read_data;
    mm::mcu::I2cConfiguration last_i2c_config{};

    [[nodiscard]] mm::mcu::Status i2c_configure(const mm::mcu::I2cConfiguration& config) override {
        if (i2c_fail) return mm::mcu::Status::TransportError;
        last_i2c_config = config;
        i2c_configured = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int /*instance*/, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (i2c_fail) return mm::mcu::Status::TransportError;
        if (!i2c_configured) return mm::mcu::Status::BadArgument;
        i2c_written_address = address;
        i2c_written.assign(data.begin(), data.end());
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_read(unsigned int /*instance*/, unsigned int /*address*/,
                                           std::span<std::byte> data) override {
        if (i2c_fail) return mm::mcu::Status::TransportError;
        if (!i2c_configured) return mm::mcu::Status::BadArgument;
        const std::size_t count = std::min(data.size(), i2c_read_data.size());
        for (std::size_t i = 0; i < count; ++i) {
            data[i] = i2c_read_data[i];
        }
        for (std::size_t i = count; i < data.size(); ++i) {
            data[i] = std::byte{0xFF};
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int /*instance*/, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (i2c_fail) return mm::mcu::Status::TransportError;
        if (!i2c_configured) return mm::mcu::Status::BadArgument;
        i2c_written_address = address;
        i2c_written.assign(command.begin(), command.end());
        const std::size_t count = std::min(data.size(), i2c_read_data.size());
        for (std::size_t i = 0; i < count; ++i) {
            data[i] = i2c_read_data[i];
        }
        for (std::size_t i = count; i < data.size(); ++i) {
            data[i] = std::byte{0xFF};
        }
        return mm::mcu::Status::Ok;
    }

    bool adc_present = true;
    bool adc_fail = false;
    bool adc_claimed[4] = {};
    unsigned int adc_count[4] = {};

    [[nodiscard]] mm::mcu::AdcDescription adc_description() const override {
        if (!adc_present) return {};
        return {test_adc_channels};
    }

    [[nodiscard]] mm::mcu::Status adc_configure(unsigned int channel) override {
        if (adc_fail) return mm::mcu::Status::TransportError;
        if (channel >= std::size(test_adc_channels)) return mm::mcu::Status::BadArgument;
        adc_claimed[channel] = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_read(unsigned int channel, unsigned int& count) override {
        if (adc_fail) return mm::mcu::Status::TransportError;
        if (channel >= std::size(test_adc_channels) || !adc_claimed[channel])
            return mm::mcu::Status::BadArgument;
        count = adc_count[channel];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_release(unsigned int channel) override {
        if (channel >= std::size(test_adc_channels)) return mm::mcu::Status::BadArgument;
        adc_claimed[channel] = false;
        return mm::mcu::Status::Ok;
    }

    bool pwm_present = true;
    bool pwm_fail = false;
    struct PwmClaim {
        bool claimed = false;
        std::uint64_t requested = 0;
        std::uint64_t duty = 0;
    };
    PwmClaim pwm_claims[5] = {};
    struct PwmGroupState {
        unsigned int members = 0;
        std::uint64_t requested = 0;
        std::uint64_t actual = 0;
    };
    PwmGroupState pwm_groups[5] = {};

    static const mm::mcu::PwmOutput* pwm_entry(unsigned int number) {
        for (const auto& out : test_pwm_outputs) {
            if (out.number == number) return &out;
        }
        return nullptr;
    }

    static std::size_t pwm_index(const mm::mcu::PwmOutput* entry) {
        for (std::size_t i = 0; i < std::size(test_pwm_outputs); ++i) {
            if (&test_pwm_outputs[i] == entry) return i;
        }
        return 0;
    }

    [[nodiscard]] mm::mcu::PwmDescription pwm_description() const override {
        if (!pwm_present) return {};
        return {test_pwm_outputs};
    }

    [[nodiscard]] mm::mcu::Status pwm_configure(unsigned int number,
                                                std::uint64_t period_ns) override {
        if (pwm_fail) return mm::mcu::Status::TransportError;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr || period_ns == 0) return mm::mcu::Status::BadArgument;
        if (entry->minimum_period_ns != 0 && entry->maximum_period_ns != 0 &&
            (period_ns < entry->minimum_period_ns || period_ns > entry->maximum_period_ns))
            return mm::mcu::Status::BadArgument;
        auto& claim = pwm_claims[pwm_index(entry)];
        if (claim.claimed) {
            return claim.requested == period_ns ? mm::mcu::Status::Ok : mm::mcu::Status::Busy;
        }
        auto& group = pwm_groups[entry->group];
        if (group.members != 0) {
            if (group.requested != period_ns) return mm::mcu::Status::Busy;
            for (std::size_t i = 0; i < std::size(test_pwm_outputs); ++i)
                if (pwm_claims[i].claimed && test_pwm_outputs[i].group == entry->group &&
                    test_pwm_outputs[i].comparator == entry->comparator)
                    return mm::mcu::Status::Busy;
        } else {
            group.requested = period_ns;
            group.actual = ((period_ns + 4) / 8) * 8;
            if (group.actual == 0) group.actual = 8;
        }
        ++group.members;
        claim.claimed = true;
        claim.requested = period_ns;
        claim.duty = 0;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_period(unsigned int number,
                                             std::uint64_t& actual_ns) override {
        if (pwm_fail) return mm::mcu::Status::TransportError;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr || !pwm_claims[pwm_index(entry)].claimed)
            return mm::mcu::Status::BadArgument;
        actual_ns = pwm_groups[entry->group].actual;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_write(unsigned int number, std::uint64_t duty_ns) override {
        if (pwm_fail) return mm::mcu::Status::TransportError;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr || !pwm_claims[pwm_index(entry)].claimed)
            return mm::mcu::Status::BadArgument;
        if (duty_ns > pwm_groups[entry->group].actual) return mm::mcu::Status::BadArgument;
        pwm_claims[pwm_index(entry)].duty = duty_ns;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_release(unsigned int number) override {
        if (pwm_fail) return mm::mcu::Status::TransportError;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr) return mm::mcu::Status::BadArgument;
        auto& claim = pwm_claims[pwm_index(entry)];
        if (!claim.claimed) return mm::mcu::Status::Ok;
        claim.claimed = false;
        claim.duty = 0;
        auto& group = pwm_groups[entry->group];
        if (--group.members == 0) group = {};
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

    bool write_fail = false;
    bool read_fail = false;

    [[nodiscard]] mm::stdio::Status write(std::span<const std::byte> data,
                                          std::size_t& written) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        if (write_fail) return mm::stdio::Status::TransportError;
        written_data.insert(written_data.end(), data.begin(), data.end());
        written = data.size();
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status read(std::span<std::byte> data,
                                         std::size_t& count) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        if (read_fail) return mm::stdio::Status::TransportError;
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

void test_set_console_write_fail(bool fail) {
    console_instance.write_fail = fail;
}

void test_console_feed_input(std::string_view input) {
    for (char c : input) {
        console_instance.pending_read.push_back(static_cast<std::byte>(c));
    }
}

void test_console_clear_input() {
    console_instance.pending_read.clear();
    console_instance.read_offset = 0;
}

std::string test_console_get_written() {
    std::string s;
    for (auto b : console_instance.written_data) {
        s.push_back(static_cast<char>(b));
    }
    return s;
}

void test_console_clear_written() {
    console_instance.written_data.clear();
}

void test_gpio_set_edge(unsigned int pin, bool pending) {
    if (pin < pin_count) {
        platform_instance.pending_edge[pin] = pending;
    }
}

bool test_gpio_is_watched(unsigned int pin) {
    if (pin < pin_count) {
        return platform_instance.watched[pin];
    }
    return false;
}

void test_gpio_clear_all_edges() {
    for (unsigned int i = 0; i < pin_count; ++i) {
        platform_instance.pending_edge[i] = false;
        platform_instance.watched[i] = false;
    }
}

void test_set_ticks_fail(bool fail) {
    platform_instance.ticks_fail = fail;
}

void test_set_delay_fail(bool fail) {
    platform_instance.delay_fail = fail;
}

void test_set_console_read_fail(bool fail) {
    console_instance.read_fail = fail;
}

void test_setup_pulse(unsigned int pin, std::initializer_list<bool> levels, unsigned long step_us) {
    platform_instance.configured[pin] = true;
    platform_instance.output[pin] = false;
    platform_instance.feed_pulse = true;
    platform_instance.feed_pulse_pin = pin;
    platform_instance.feed_pulse_levels = levels;
    platform_instance.feed_pulse_index = 0;
    platform_instance.pulse_step_us = step_us;
}

void test_clear_pulse() {
    platform_instance.feed_pulse = false;
    platform_instance.feed_pulse_levels.clear();
}

void test_set_spi_present(bool present) {
    platform_instance.spi_present = present;
}

void test_set_spi_fail(bool fail) {
    platform_instance.spi_fail = fail;
}

void test_set_spi_xor_mask(unsigned char mask) {
    platform_instance.spi_xor_mask = mask;
}

void test_reset_spi() {
    platform_instance.spi_configured = false;
    platform_instance.spi_xor_mask = 0;
    platform_instance.spi_fail = false;
    platform_instance.spi_present = true;
}

void test_set_i2c_present(bool present) {
    platform_instance.i2c_present = present;
}

void test_set_i2c_fail(bool fail) {
    platform_instance.i2c_fail = fail;
}

void test_set_i2c_read_data(const unsigned char* data, std::size_t size) {
    platform_instance.i2c_read_data.clear();
    for (std::size_t i = 0; i < size; ++i) {
        platform_instance.i2c_read_data.push_back(static_cast<std::byte>(data[i]));
    }
}

std::size_t test_get_i2c_written_size() {
    return platform_instance.i2c_written.size();
}

unsigned char test_get_i2c_written_byte(std::size_t idx) {
    return static_cast<unsigned char>(platform_instance.i2c_written[idx]);
}

unsigned int test_get_i2c_written_address() {
    return platform_instance.i2c_written_address;
}

void test_reset_i2c() {
    platform_instance.i2c_configured = false;
    platform_instance.i2c_fail = false;
    platform_instance.i2c_present = true;
    platform_instance.i2c_written.clear();
    platform_instance.i2c_written_address = 0;
    platform_instance.i2c_read_data.clear();
}

void test_adc_set_count(unsigned int channel, unsigned int count) {
    if (channel < 4) platform_instance.adc_count[channel] = count;
}

bool test_adc_is_configured(unsigned int channel) {
    if (channel < 4) return platform_instance.adc_claimed[channel];
    return false;
}

void test_set_adc_present(bool present) {
    platform_instance.adc_present = present;
}

void test_set_adc_fail(bool fail) {
    platform_instance.adc_fail = fail;
}

void test_reset_adc() {
    platform_instance.adc_present = true;
    platform_instance.adc_fail = false;
    for (int i = 0; i < 4; ++i) {
        platform_instance.adc_claimed[i] = false;
        platform_instance.adc_count[i] = 0;
    }
}

bool test_pwm_is_configured(unsigned int output) {
    const auto* entry = platform_instance.pwm_entry(output);
    if (!entry) return false;
    return platform_instance.pwm_claims[platform_instance.pwm_index(entry)].claimed;
}

std::uint64_t test_pwm_get_duty(unsigned int output) {
    const auto* entry = platform_instance.pwm_entry(output);
    if (!entry) return 0;
    return platform_instance.pwm_claims[platform_instance.pwm_index(entry)].duty;
}

std::uint64_t test_pwm_get_actual_period(unsigned int output) {
    const auto* entry = platform_instance.pwm_entry(output);
    if (!entry) return 0;
    return platform_instance.pwm_groups[entry->group].actual;
}

void test_set_pwm_present(bool present) {
    platform_instance.pwm_present = present;
}

void test_set_pwm_fail(bool fail) {
    platform_instance.pwm_fail = fail;
}

void test_reset_pwm() {
    platform_instance.pwm_present = true;
    platform_instance.pwm_fail = false;
    for (auto& c : platform_instance.pwm_claims) c = {};
    for (auto& g : platform_instance.pwm_groups) g = {};
}

