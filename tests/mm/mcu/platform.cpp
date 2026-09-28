// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A stand-in platform for mm.mcu, and a demonstration of the supply mechanism
// rather than a mock of it: it registers a Platform subclass exactly as a real
// platform's module does, and nothing here is scaffolding a real platform would
// not also have to provide.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
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

// The analog inventories. Three pin channels and an internal one; one channel
// with no known reference, so the conversion's zero answer is exercised. The
// first two pin channels can be paced, the third and the internal one cannot,
// and a paced channel buffers eight counts. The
// PWM outputs are numbered by GPIO as the Pico numbers them: 0 and 1 are the
// two comparators of one counter, 16 is the alias of 0's comparator, 2 has a
// counter of its own, and 3 is an output whose limits the stand-in does not
// know.
constexpr mm::mcu::AdcChannel adc_channels[] = {
    {0, "ADC0", 26, 12, 3300, 500'000, 8},
    {1, "ADC1", 27, 12, 3300, 500'000, 8},
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

// Two DAC outputs: a ten-bit PWM-backed one on a pad, with rate limits, and a
// twelve-bit converter on no pad whose limits the stand-in does not know. Each
// queues four levels.
constexpr mm::mcu::DacOutput dac_outputs[] = {
    {0, "DAC0", 20, 10, true, 8'000, 96'000, 4},
    {1, "DAC1", std::nullopt, 12, false, 0, 0, 4},
};

// The clocks the stand-in divides its rates from, so that an actual rate is
// generally not the requested one and the exact report has something to say.
constexpr std::uint64_t adc_clock_hz = 48'000'000;
constexpr std::uint64_t dac_clock_hz = 1'000'000;
constexpr std::uint64_t i2s_clock_hz = 12'288'000;

// Frames the stand-in's I2S buffers hold in each direction.
constexpr std::size_t i2s_depth = 4;

// The nearest whole divisor of clock for rate, never zero.
std::uint64_t divisor_for(std::uint64_t clock, unsigned long rate) {
    const std::uint64_t divisor = (clock + rate / 2) / rate;
    return divisor == 0 ? 1 : divisor;
}

// One frame on the I2S link, left word then right.
struct Frame {
    std::int32_t left = 0;
    std::int32_t right = 0;
};

// One direction of the link.
struct Lane {
    bool started = false;
    std::vector<Frame> queue;
    mm::mcu::Progress progress;
};

// Who holds a pad. A plain GPIO configuration yields to an analog claim; a
// watch and an analog claim yield only to their own release.
enum class Owner { None, Gpio, Watched, Adc, Pwm, Dac, I2s };

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
        if (paced && *paced != channel) return mm::mcu::Status::Busy;
        if (adc_claimed[channel]) return mm::mcu::Status::Ok;
        if (entry->gpio) {
            const auto pin = *entry->gpio;
            if (owner[pin] != Owner::None && owner[pin] != Owner::Gpio)
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
        if (paced) return mm::mcu::Status::Busy;
        count = adc_count[channel];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_release(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = adc_entry(channel);
        if (entry == nullptr) return mm::mcu::Status::BadArgument;
        if (adc_claimed[channel] && entry->gpio) owner[*entry->gpio] = Owner::None;
        adc_claimed[channel] = false;
        if (paced && *paced == channel) {
            paced.reset();
            pace_started = false;
            pace_queue.clear();
        }
        return mm::mcu::Status::Ok;
    }

    // Paced capture owns the whole converter. Everything is validated before
    // anything is recorded, so a refused pace leaves the earlier state true.
    [[nodiscard]] mm::mcu::Status adc_pace(unsigned int channel, unsigned long rate_hz) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = adc_entry(channel);
        if (entry == nullptr || rate_hz == 0 || entry->maximum_pace_hz == 0 ||
            rate_hz > entry->maximum_pace_hz)
            return mm::mcu::Status::BadArgument;
        if (paced && (*paced != channel || pace_started)) return mm::mcu::Status::Busy;
        const auto claimed = adc_configure(channel);
        if (claimed != mm::mcu::Status::Ok) return claimed;
        paced = channel;
        pace_divisor = divisor_for(adc_clock_hz, rate_hz);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_pace_rate(unsigned int channel,
                                                mm::mcu::Frequency& actual) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!paced || *paced != channel) return mm::mcu::Status::BadArgument;
        actual = {adc_clock_hz, pace_divisor};
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_pace_start(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!paced || *paced != channel) return mm::mcu::Status::BadArgument;
        pace_queue.clear();
        pace_progress = {};
        pace_started = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_take(unsigned int channel, std::span<std::uint16_t> counts,
                                           std::size_t& count) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!paced || *paced != channel) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(counts.size(), pace_queue.size());
        std::copy_n(pace_queue.begin(), moved, counts.begin());
        pace_queue.erase(pace_queue.begin(), pace_queue.begin() + static_cast<long>(moved));
        count = moved;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_pace_progress(unsigned int channel,
                                                    mm::mcu::Progress& progress) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!paced || *paced != channel) return mm::mcu::Status::BadArgument;
        progress = pace_progress;
        progress.queued = pace_queue.size();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status adc_pace_stop(unsigned int channel) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!paced || *paced != channel) return mm::mcu::Status::BadArgument;
        pace_started = false;
        pace_queue.clear();
        return mm::mcu::Status::Ok;
    }

    // One conversion arriving at the paced channel, as the converter's clock
    // would deliver it: counted, and buffered or dropped.
    void convert(std::uint16_t count) {
        if (!paced || !pace_started) return;
        ++pace_progress.completed;
        if (pace_queue.size() < adc_entry(*paced)->pace_depth)
            pace_queue.push_back(count);
        else
            ++pace_progress.missed;
    }

    [[nodiscard]] mm::mcu::DacDescription dac_description() const override {
        return {dac_outputs};
    }

    [[nodiscard]] mm::mcu::Status dac_configure(unsigned int number,
                                                unsigned long rate_hz) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = dac_entry(number);
        if (entry == nullptr || rate_hz == 0) return mm::mcu::Status::BadArgument;
        if (entry->minimum_rate_hz != 0 && entry->maximum_rate_hz != 0 &&
            (rate_hz < entry->minimum_rate_hz || rate_hz > entry->maximum_rate_hz))
            return mm::mcu::Status::BadArgument;
        auto& dac = dacs[dac_index(entry)];
        if (dac.claimed && dac.started) return mm::mcu::Status::Busy;
        if (!dac.claimed && entry->gpio) {
            const auto pin = *entry->gpio;
            if (owner[pin] != Owner::None && owner[pin] != Owner::Gpio)
                return mm::mcu::Status::Busy;
            configured[pin] = false;
            output[pin] = false;
            owner[pin] = Owner::Dac;
        }
        dac.claimed = true;
        dac.divisor = divisor_for(dac_clock_hz, rate_hz);
        dac.level = half_scale(*entry);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status dac_rate(unsigned int number,
                                           mm::mcu::Frequency& actual) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* dac = claimed_dac(number);
        if (dac == nullptr) return mm::mcu::Status::BadArgument;
        actual = {dac_clock_hz, dac->divisor};
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status dac_start(unsigned int number) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        auto* dac = claimed_dac(number);
        if (dac == nullptr) return mm::mcu::Status::BadArgument;
        dac->progress = {};
        dac->started = true;
        return mm::mcu::Status::Ok;
    }

    // A level outside the output's width is refused whole, so a caller never
    // learns afterwards that part of what it gave was played.
    [[nodiscard]] mm::mcu::Status dac_give(unsigned int number,
                                           std::span<const std::uint16_t> levels,
                                           std::size_t& accepted) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        auto* dac = claimed_dac(number);
        if (dac == nullptr) return mm::mcu::Status::BadArgument;
        const auto* entry = dac_entry(number);
        for (const auto level : levels)
            if (level >= (1u << entry->bits)) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(levels.size(), entry->depth - dac->queue.size());
        dac->queue.insert(dac->queue.end(), levels.begin(),
                          levels.begin() + static_cast<long>(moved));
        accepted = moved;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status dac_progress(unsigned int number,
                                               mm::mcu::Progress& progress) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* dac = claimed_dac(number);
        if (dac == nullptr) return mm::mcu::Status::BadArgument;
        progress = dac->progress;
        progress.queued = dac->queue.size();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status dac_stop(unsigned int number) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        auto* dac = claimed_dac(number);
        if (dac == nullptr) return mm::mcu::Status::BadArgument;
        dac->started = false;
        dac->queue.clear();
        dac->level = half_scale(*dac_entry(number));
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status dac_release(unsigned int number) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* entry = dac_entry(number);
        if (entry == nullptr) return mm::mcu::Status::BadArgument;
        auto& dac = dacs[dac_index(entry)];
        if (dac.claimed && entry->gpio) owner[*entry->gpio] = Owner::None;
        dac = {};
        return mm::mcu::Status::Ok;
    }

    // Level periods passing on an output, as its clock would run them: a
    // queued level is output and counted, an empty queue outputs half scale and
    // counts a miss. Idle, the output simply holds half scale.
    void dac_tick(unsigned int number, std::size_t periods) {
        const auto* entry = dac_entry(number);
        if (entry == nullptr) return;
        auto& dac = dacs[dac_index(entry)];
        for (std::size_t i = 0; i < periods && dac.claimed; ++i) {
            if (!dac.started) {
                dac.level = half_scale(*entry);
            } else if (dac.queue.empty()) {
                dac.level = half_scale(*entry);
                ++dac.progress.missed;
            } else {
                dac.level = dac.queue.front();
                dac.queue.erase(dac.queue.begin());
                ++dac.progress.completed;
            }
        }
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
            if (owner[pin] != Owner::None && owner[pin] != Owner::Gpio &&
                owner[pin] != Owner::Pwm)
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

    // The link as the contract describes it: validated whole before anything is
    // claimed, one configuration per instance, clocks running from here until
    // release. The stand-in carries sixteen-, twenty-four-, and thirty-two-bit
    // slots, and drives any wiring whose pins are distinct.
    [[nodiscard]] mm::mcu::Status i2s_configure(
        const mm::mcu::I2sConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.instance > 1 || configuration.rate_hz == 0 ||
            (configuration.slot_bits != 16 && configuration.slot_bits != 24 &&
             configuration.slot_bits != 32) ||
            (!configuration.transmit_gpio && !configuration.receive_gpio))
            return mm::mcu::Status::BadArgument;
        unsigned int pins[4] = {configuration.bit_clock_gpio, configuration.word_clock_gpio};
        std::size_t pin_total = 2;
        if (configuration.transmit_gpio) pins[pin_total++] = *configuration.transmit_gpio;
        if (configuration.receive_gpio) pins[pin_total++] = *configuration.receive_gpio;
        for (std::size_t i = 0; i < pin_total; ++i) {
            if (pins[i] >= pin_count) return mm::mcu::Status::BadArgument;
            for (std::size_t j = 0; j < i; ++j)
                if (pins[i] == pins[j]) return mm::mcu::Status::BadArgument;
        }
        if (i2s_ready) {
            return same_link(configuration) ? mm::mcu::Status::Ok : mm::mcu::Status::Busy;
        }
        for (std::size_t i = 0; i < pin_total; ++i)
            if (owner[pins[i]] != Owner::None && owner[pins[i]] != Owner::Gpio)
                return mm::mcu::Status::Busy;
        for (std::size_t i = 0; i < pin_total; ++i) {
            configured[pins[i]] = false;
            output[pins[i]] = false;
            owner[pins[i]] = Owner::I2s;
        }
        i2s_configuration = configuration;
        i2s_ready = true;
        i2s_divisor = divisor_for(i2s_clock_hz, configuration.rate_hz);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_rate(unsigned int instance,
                                           mm::mcu::Frequency& actual) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2s_on(instance)) return mm::mcu::Status::BadArgument;
        actual = {i2s_clock_hz, i2s_divisor};
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_start(unsigned int instance,
                                            mm::mcu::I2sDirection direction) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        auto* lane = i2s_lane(instance, direction);
        if (lane == nullptr) return mm::mcu::Status::BadArgument;
        if (direction == mm::mcu::I2sDirection::Receive) lane->queue.clear();
        lane->progress = {};
        lane->started = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_write(unsigned int instance,
                                            std::span<const std::int16_t> words,
                                            std::size_t& accepted) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (i2s_configuration.slot_bits != 16) return mm::mcu::Status::BadArgument;
        std::vector<Frame> frames;
        for (std::size_t i = 0; i + 1 < words.size(); i += 2)
            frames.push_back({words[i], words[i + 1]});
        return queue_frames(instance, frames, accepted);
    }

    [[nodiscard]] mm::mcu::Status i2s_write(unsigned int instance,
                                            std::span<const std::int32_t> words,
                                            std::size_t& accepted) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (i2s_configuration.slot_bits == 16) return mm::mcu::Status::BadArgument;
        std::vector<Frame> frames;
        for (std::size_t i = 0; i + 1 < words.size(); i += 2)
            frames.push_back({words[i], words[i + 1]});
        return queue_frames(instance, frames, accepted);
    }

    [[nodiscard]] mm::mcu::Status i2s_read(unsigned int instance,
                                           std::span<std::int16_t> words,
                                           std::size_t& count) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (i2s_configuration.slot_bits != 16) return mm::mcu::Status::BadArgument;
        auto* lane = i2s_lane(instance, mm::mcu::I2sDirection::Receive);
        if (lane == nullptr) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(words.size() / 2, lane->queue.size());
        for (std::size_t i = 0; i < moved; ++i) {
            words[2 * i] = static_cast<std::int16_t>(lane->queue[i].left);
            words[2 * i + 1] = static_cast<std::int16_t>(lane->queue[i].right);
        }
        lane->queue.erase(lane->queue.begin(), lane->queue.begin() + static_cast<long>(moved));
        count = moved;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_read(unsigned int instance,
                                           std::span<std::int32_t> words,
                                           std::size_t& count) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (i2s_configuration.slot_bits == 16) return mm::mcu::Status::BadArgument;
        auto* lane = i2s_lane(instance, mm::mcu::I2sDirection::Receive);
        if (lane == nullptr) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(words.size() / 2, lane->queue.size());
        for (std::size_t i = 0; i < moved; ++i) {
            words[2 * i] = lane->queue[i].left;
            words[2 * i + 1] = lane->queue[i].right;
        }
        lane->queue.erase(lane->queue.begin(), lane->queue.begin() + static_cast<long>(moved));
        count = moved;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_progress(unsigned int instance,
                                               mm::mcu::I2sDirection direction,
                                               mm::mcu::Progress& progress) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        const auto* lane = i2s_lane(instance, direction);
        if (lane == nullptr) return mm::mcu::Status::BadArgument;
        progress = lane->progress;
        progress.queued = lane->queue.size();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_stop(unsigned int instance,
                                           mm::mcu::I2sDirection direction) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        auto* lane = i2s_lane(instance, direction);
        if (lane == nullptr) return mm::mcu::Status::BadArgument;
        lane->started = false;
        lane->queue.clear();
        if (direction == mm::mcu::I2sDirection::Transmit) i2s_sent = {};
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_release(unsigned int instance) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (instance > 1) return mm::mcu::Status::BadArgument;
        if (!i2s_on(instance)) return mm::mcu::Status::Ok;
        for (auto& entry : owner)
            if (entry == Owner::I2s) entry = Owner::None;
        reset_i2s();
        return mm::mcu::Status::Ok;
    }

    // Frames passing on the transmitter, as the word clock runs them: a queued
    // frame is sent and counted, an empty queue sends zeros and counts a miss.
    void i2s_tick(std::size_t frames) {
        auto& lane = i2s_transmit;
        for (std::size_t i = 0; i < frames && i2s_ready; ++i) {
            if (!lane.started) {
                i2s_sent = {};
            } else if (lane.queue.empty()) {
                i2s_sent = {};
                ++lane.progress.missed;
            } else {
                i2s_sent = lane.queue.front();
                lane.queue.erase(lane.queue.begin());
                ++lane.progress.completed;
            }
        }
    }

    // One frame arriving at the receiver: counted, and buffered or dropped.
    void i2s_receive(std::int32_t left, std::int32_t right) {
        auto& lane = i2s_received;
        if (!i2s_ready || !lane.started) return;
        ++lane.progress.completed;
        if (lane.queue.size() < i2s_depth)
            lane.queue.push_back({left, right});
        else
            ++lane.progress.missed;
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

    // Nesting as a mask register behaves: each disable saves the depth it
    // found, and an enable must hand back the innermost one, so sections that
    // end out of order are caught rather than silently unmasking.
    [[nodiscard]] mm::mcu::Status interrupts_disable(std::uint32_t& saved) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        saved = interrupt_depth;
        ++interrupt_depth;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status interrupts_enable(std::uint32_t saved) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (interrupt_depth == 0 || saved != interrupt_depth - 1)
            return mm::mcu::Status::BadArgument;
        --interrupt_depth;
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
            owner[pin] = Owner::None;
        }
        for (auto& claimed : adc_claimed) claimed = false;
        for (auto& count : adc_count) count = 0;
        for (auto& claim : pwm_claims) claim = {};
        for (auto& group : pwm_groups) group = {};
        forced = mm::mcu::Status::Ok;
        ticks = 0;
        interrupt_depth = 0;
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
        paced.reset();
        pace_divisor = 0;
        pace_started = false;
        pace_queue.clear();
        pace_progress = {};
        for (auto& dac : dacs) dac = {};
        reset_i2s();
    }

    void reset_i2s() {
        i2s_ready = false;
        i2s_configuration = {};
        i2s_divisor = 0;
        i2s_transmit = {};
        i2s_received = {};
        i2s_sent = {};
    }

    [[nodiscard]] bool i2s_on(unsigned int instance) const {
        return i2s_ready && instance == i2s_configuration.instance;
    }

    [[nodiscard]] bool same_link(const mm::mcu::I2sConfiguration& other) const {
        const auto& own = i2s_configuration;
        return own.instance == other.instance && own.bit_clock_gpio == other.bit_clock_gpio &&
               own.word_clock_gpio == other.word_clock_gpio &&
               own.transmit_gpio == other.transmit_gpio &&
               own.receive_gpio == other.receive_gpio && own.rate_hz == other.rate_hz &&
               own.slot_bits == other.slot_bits;
    }

    // The lane for a direction the configured link carries, or nothing.
    [[nodiscard]] Lane* i2s_lane(unsigned int instance, mm::mcu::I2sDirection direction) {
        if (!i2s_on(instance)) return nullptr;
        if (direction == mm::mcu::I2sDirection::Transmit)
            return i2s_configuration.transmit_gpio ? &i2s_transmit : nullptr;
        return i2s_configuration.receive_gpio ? &i2s_received : nullptr;
    }

    // Queues as many of frames as the transmitter has room for.
    [[nodiscard]] mm::mcu::Status queue_frames(unsigned int instance,
                                               const std::vector<Frame>& frames,
                                               std::size_t& accepted) {
        auto* lane = i2s_lane(instance, mm::mcu::I2sDirection::Transmit);
        if (lane == nullptr) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(frames.size(), i2s_depth - lane->queue.size());
        lane->queue.insert(lane->queue.end(), frames.begin(),
                           frames.begin() + static_cast<long>(moved));
        accepted = moved;
        return mm::mcu::Status::Ok;
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
        return owner[pin] == Owner::Adc || owner[pin] == Owner::Pwm ||
               owner[pin] == Owner::Dac || owner[pin] == Owner::I2s;
    }

    [[nodiscard]] static const mm::mcu::DacOutput* dac_entry(unsigned int number) {
        for (const auto& entry : dac_outputs)
            if (entry.number == number) return &entry;
        return nullptr;
    }

    [[nodiscard]] static std::size_t dac_index(const mm::mcu::DacOutput* entry) {
        return static_cast<std::size_t>(entry - dac_outputs);
    }

    [[nodiscard]] static std::uint16_t half_scale(const mm::mcu::DacOutput& entry) {
        return static_cast<std::uint16_t>(1u << (entry.bits - 1));
    }

    struct Dac {
        bool claimed = false;
        bool started = false;
        std::uint64_t divisor = 0;
        std::uint16_t level = 0;
        std::vector<std::uint16_t> queue;
        mm::mcu::Progress progress;
    };

    [[nodiscard]] Dac* claimed_dac(unsigned int number) {
        const auto* entry = dac_entry(number);
        if (entry == nullptr || !dacs[dac_index(entry)].claimed) return nullptr;
        return &dacs[dac_index(entry)];
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
    std::uint32_t interrupt_depth = 0;
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
    std::optional<unsigned int> paced;
    std::uint64_t pace_divisor = 0;
    bool pace_started = false;
    std::vector<std::uint16_t> pace_queue;
    mm::mcu::Progress pace_progress;
    Dac dacs[std::size(dac_outputs)] = {};
    bool i2s_ready = false;
    mm::mcu::I2sConfiguration i2s_configuration;
    std::uint64_t i2s_divisor = 0;
    Lane i2s_transmit;
    Lane i2s_received;
    Frame i2s_sent;
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
unsigned int mm_test_interrupt_depth() { return stand.interrupt_depth; }
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
bool mm_test_i2s_ready() { return stand.i2s_ready; }
void mm_test_i2s_tick(std::size_t frames) { stand.i2s_tick(frames); }
void mm_test_i2s_receive(std::int32_t left, std::int32_t right) {
    stand.i2s_receive(left, right);
}
std::int32_t mm_test_i2s_sent_left() { return stand.i2s_sent.left; }
std::int32_t mm_test_i2s_sent_right() { return stand.i2s_sent.right; }
void mm_test_adc_convert(std::uint16_t count) { stand.convert(count); }
void mm_test_dac_tick(unsigned int output, std::size_t periods) {
    stand.dac_tick(output, periods);
}
unsigned int mm_test_dac_level(unsigned int output) {
    const auto* entry = Stand::dac_entry(output);
    return entry == nullptr ? 0 : stand.dacs[Stand::dac_index(entry)].level;
}
void mm_test_adc_set_count(unsigned int channel, unsigned int count) {
    if (channel < std::size(adc_channels)) stand.adc_count[channel] = count;
}
bool mm_test_adc_claimed(unsigned int channel) {
    return channel < std::size(adc_channels) && stand.adc_claimed[channel];
}
// 0 none, 1 gpio, 2 watched, 3 adc, 4 pwm, 5 dac, 6 i2s.
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
