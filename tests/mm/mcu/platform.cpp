// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A stand-in platform for mm.mcu, and a demonstration of the supply mechanism
// rather than a mock of it: it registers a Platform subclass exactly as a real
// platform's module does, and nothing here is scaffolding a real platform would
// not also have to provide.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

// The analog inventories. Three pin channels and an internal one; one channel
// with no known reference, so the conversion's zero answer is exercised. The
// PWM outputs are numbered by GPIO as the Pico numbers them: 0 and 1 are the
// two comparators of one counter, 16 is the alias of 0's comparator, 2 has a
// counter of its own, and 3 is an output whose limits the stand-in does not
// know.
constexpr mm::mcu::AdcChannel adc_channels[] = {
    {0, "ADC0", 26, 12, 3300},
    {1, "ADC1", 27, 12, 3300},
    {2, "ADC2", 28, 12, 0},
    {3, "TEMP", std::nullopt, 12, 3300},
};

constexpr std::uint64_t pwm_shortest = 16;
constexpr std::uint64_t pwm_longest = 134'000'000;

constexpr mm::mcu::PwmOutput pwm_outputs[] = {
    {0, "PWM0", 0, 0, 0, pwm_shortest, pwm_longest},
    {1, "PWM1", 1, 0, 1, pwm_shortest, pwm_longest},
    {16, "PWM16", 16, 0, 0, pwm_shortest, pwm_longest},
    {2, "PWM2", 2, 1, 0, pwm_shortest, pwm_longest},
    {3, "PWM3", 3, 2, 0, 0, 0},
};

constexpr unsigned int group_count = 3;

// Who holds a pad. A plain GPIO configuration yields to an analog claim; a
// watch and an analog claim yield only to their own release.
enum class Owner { None, Gpio, Watched, Adc, Pwm };

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
        if (analog_holds(pin)) return mm::mcu::Status::Busy;
        configured[pin] = true;
        output[pin] = direction == mm::mcu::Direction::Out;
        owner[pin] = Owner::Gpio;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (analog_holds(pin)) return mm::mcu::Status::Busy;
        if (!configured[pin]) return mm::mcu::Status::BadArgument;
        if (!output[pin]) return mm::mcu::Status::Unsupported;
        level[pin] = high;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (analog_holds(pin)) return mm::mcu::Status::Busy;
        if (!configured[pin]) return mm::mcu::Status::BadArgument;
        high = level[pin];
        return mm::mcu::Status::Ok;
    }

    // The latch, as far as ownership needs it: a watched pad is one no
    // analog claim may take, and nothing here delivers an edge.
    [[nodiscard]] mm::mcu::Status gpio_watch(unsigned int pin, mm::mcu::Pull,
                                              mm::mcu::Edge) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (analog_holds(pin)) return mm::mcu::Status::Busy;
        configured[pin] = true;
        output[pin] = false;
        owner[pin] = Owner::Watched;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_take(unsigned int pin, bool& pending) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count || owner[pin] != Owner::Watched) return mm::mcu::Status::BadArgument;
        pending = false;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_unwatch(unsigned int pin) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (owner[pin] == Owner::Watched) {
            owner[pin] = Owner::None;
            configured[pin] = false;
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::AdcDescription adc_description() const override {
        return {adc_channels};
    }

    [[nodiscard]] mm::mcu::Status adc_configure(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = adc_entry(channel);
        if (entry == nullptr) return mm::mcu::Status::BadArgument;
        if (adc_claimed[channel]) return mm::mcu::Status::Ok;
        if (entry->gpio) {
            const auto pin = *entry->gpio;
            if (owner[pin] == Owner::Watched || owner[pin] == Owner::Pwm)
                return mm::mcu::Status::Busy;
            // The takeover: whatever digital mode the pad had is gone.
            configured[pin] = false;
            output[pin] = false;
            owner[pin] = Owner::Adc;
        }
        adc_claimed[channel] = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_read(unsigned int channel, unsigned int& count) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (adc_entry(channel) == nullptr || !adc_claimed[channel])
            return mm::mcu::Status::BadArgument;
        count = adc_count[channel];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_release(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = adc_entry(channel);
        if (entry == nullptr) return mm::mcu::Status::BadArgument;
        if (adc_claimed[channel] && entry->gpio) owner[*entry->gpio] = Owner::None;
        adc_claimed[channel] = false;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::PwmDescription pwm_description() const override {
        return {pwm_outputs};
    }

    // Everything is validated before anything is recorded, in the order the
    // contract lists: inventory, period, ownership, then group and alias.
    [[nodiscard]] mm::mcu::Status pwm_configure(unsigned int number,
                                                std::uint64_t period_ns) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr || period_ns == 0) return mm::mcu::Status::BadArgument;
        if (entry->minimum_period_ns != 0 && entry->maximum_period_ns != 0 &&
            (period_ns < entry->minimum_period_ns || period_ns > entry->maximum_period_ns))
            return mm::mcu::Status::BadArgument;
        auto& claim = pwm_claims[index_of(entry)];
        if (claim.claimed) {
            return claim.requested == period_ns ? mm::mcu::Status::Ok : mm::mcu::Status::Busy;
        }
        if (entry->gpio) {
            const auto pin = *entry->gpio;
            if (owner[pin] == Owner::Watched || owner[pin] == Owner::Adc)
                return mm::mcu::Status::Busy;
        }
        auto& group = pwm_groups[entry->group];
        if (group.members != 0) {
            if (group.requested != period_ns) return mm::mcu::Status::Busy;
            for (std::size_t i = 0; i < std::size(pwm_outputs); ++i)
                if (pwm_claims[i].claimed && pwm_outputs[i].group == entry->group &&
                    pwm_outputs[i].comparator == entry->comparator)
                    return mm::mcu::Status::Busy;
        } else {
            group.requested = period_ns;
            // The nearest period this stand-in holds: a whole number of
            // eight-nanosecond ticks, so that pwm_period has something to say
            // that the request did not.
            group.actual = ((period_ns + 4) / 8) * 8;
            if (group.actual == 0) group.actual = 8;
        }
        if (entry->gpio) {
            configured[*entry->gpio] = false;
            output[*entry->gpio] = false;
            owner[*entry->gpio] = Owner::Pwm;
        }
        ++group.members;
        claim.claimed = true;
        claim.requested = period_ns;
        claim.duty = 0;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_period(unsigned int number,
                                             std::uint64_t& actual_ns) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr || !pwm_claims[index_of(entry)].claimed)
            return mm::mcu::Status::BadArgument;
        actual_ns = pwm_groups[entry->group].actual;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_write(unsigned int number, std::uint64_t duty_ns) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr || !pwm_claims[index_of(entry)].claimed)
            return mm::mcu::Status::BadArgument;
        if (duty_ns > pwm_groups[entry->group].actual) return mm::mcu::Status::BadArgument;
        pwm_claims[index_of(entry)].duty = duty_ns;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_release(unsigned int number) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = pwm_entry(number);
        if (entry == nullptr) return mm::mcu::Status::BadArgument;
        auto& claim = pwm_claims[index_of(entry)];
        if (!claim.claimed) return mm::mcu::Status::Ok;
        claim.claimed = false;
        claim.duty = 0;
        if (entry->gpio) owner[*entry->gpio] = Owner::None;
        auto& group = pwm_groups[entry->group];
        if (--group.members == 0) group = {};
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
        ticks_us_val += milliseconds * 1000UL;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& out) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        out = ticks;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_us(unsigned long microseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks_us_val += microseconds;
        ticks += microseconds / 1000UL;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_us(unsigned long& out) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        out = ticks_us_val;
        return mm::mcu::Status::Ok;
    }

    unsigned long ticks_us_val = 0;

    void reset() {
        for (unsigned int pin = 0; pin < pin_count; ++pin) {
            configured[pin] = false;
            output[pin] = false;
            level[pin] = false;
            owner[pin] = Owner::None;
        }
        for (auto& claimed : adc_claimed) claimed = false;
        for (auto& count : adc_count) count = 0;
        for (auto& claim : pwm_claims) claim = {};
        for (auto& group : pwm_groups) group = {};
        forced = mm::mcu::Status::Ok;
        ticks = 0;
        ticks_us_val = 0;
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

    [[nodiscard]] bool analog_holds(unsigned int pin) const {
        return owner[pin] == Owner::Adc || owner[pin] == Owner::Pwm;
    }

    [[nodiscard]] static const mm::mcu::AdcChannel* adc_entry(unsigned int channel) {
        for (const auto& entry : adc_channels)
            if (entry.number == channel) return &entry;
        return nullptr;
    }

    [[nodiscard]] static const mm::mcu::PwmOutput* pwm_entry(unsigned int number) {
        for (const auto& entry : pwm_outputs)
            if (entry.number == number) return &entry;
        return nullptr;
    }

    [[nodiscard]] static std::size_t index_of(const mm::mcu::PwmOutput* entry) {
        return static_cast<std::size_t>(entry - pwm_outputs);
    }

    struct PwmClaim {
        bool claimed = false;
        std::uint64_t requested = 0;
        std::uint64_t duty = 0;
    };

    struct PwmGroup {
        unsigned int members = 0;
        std::uint64_t requested = 0;
        std::uint64_t actual = 0;
    };

    bool configured[pin_count] = {};
    bool output[pin_count] = {};
    bool level[pin_count] = {};
    Owner owner[pin_count] = {};
    bool adc_claimed[std::size(adc_channels)] = {};
    unsigned int adc_count[std::size(adc_channels)] = {};
    PwmClaim pwm_claims[std::size(pwm_outputs)] = {};
    PwmGroup pwm_groups[group_count] = {};
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
void mm_test_adc_set_count(unsigned int channel, unsigned int count) {
    if (channel < std::size(adc_channels)) stand.adc_count[channel] = count;
}
bool mm_test_adc_claimed(unsigned int channel) {
    return channel < std::size(adc_channels) && stand.adc_claimed[channel];
}
// 0 none, 1 gpio, 2 watched, 3 adc, 4 pwm.
int mm_test_pin_owner(unsigned int pin) {
    return pin < pin_count ? static_cast<int>(stand.owner[pin]) : -1;
}
bool mm_test_gpio_configured(unsigned int pin) {
    return pin < pin_count && stand.configured[pin];
}
std::uint64_t mm_test_pwm_duty(unsigned int number) {
    const auto* entry = Stand::pwm_entry(number);
    return entry == nullptr ? 0 : stand.pwm_claims[Stand::index_of(entry)].duty;
}
unsigned int mm_test_pwm_group_members(unsigned int group) {
    return group < group_count ? stand.pwm_groups[group].members : 0;
}
