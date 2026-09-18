// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

export module platform.pico.mcu;

import mm.mcu;

namespace {

constexpr mm::mcu::Gpio gpios[] = {
    {0, "GP0"},   {1, "GP1"},   {2, "GP2"},   {3, "GP3"},   {4, "GP4"},
    {5, "GP5"},   {6, "GP6"},   {7, "GP7"},   {8, "GP8"},   {9, "GP9"},
    {10, "GP10"}, {11, "GP11"}, {12, "GP12"}, {13, "GP13"}, {14, "GP14"},
    {15, "GP15"}, {16, "GP16"}, {17, "GP17"}, {18, "GP18"}, {19, "GP19"},
    {20, "GP20"}, {21, "GP21"}, {22, "GP22"}, {23, "GP23"}, {24, "GP24"},
    {25, "GP25"}, {26, "GP26"}, {27, "GP27"}, {28, "GP28"}, {29, "GP29"},
    {30, "GP30"}, {31, "GP31"}, {32, "GP32"}, {33, "GP33"}, {34, "GP34"},
    {35, "GP35"}, {36, "GP36"}, {37, "GP37"}, {38, "GP38"}, {39, "GP39"},
    {40, "GP40"}, {41, "GP41"}, {42, "GP42"}, {43, "GP43"}, {44, "GP44"},
    {45, "GP45"}, {46, "GP46"}, {47, "GP47"},
};

// Status codes cross the C boundary as integers because the boundary is C. This
// is the only place they are integers: above it the interface is typed, and below
// it the adapter uses the same constants.
mm::mcu::Status from(int code) {
    switch (code) {
        case MM_PICO_MCU_OK: return mm::mcu::Status::Ok;
        case MM_PICO_MCU_BAD_ARGUMENT: return mm::mcu::Status::BadArgument;
        case MM_PICO_MCU_UNSUPPORTED: return mm::mcu::Status::Unsupported;
        case MM_PICO_MCU_BUSY: return mm::mcu::Status::Busy;
        case MM_PICO_MCU_TIMEOUT: return mm::mcu::Status::Timeout;
        case MM_PICO_MCU_TRANSPORT_ERROR: return mm::mcu::Status::TransportError;
        default: return mm::mcu::Status::BadArgument;
    }
}

// The analog inventories, filled once from the adapter's facts. Nine channels
// is the RP2350B's count and forty-eight outputs the largest bank; the spans
// answered are as long as the selected part has.
constexpr std::string_view adc_names[] = {"ADC0", "ADC1", "ADC2", "ADC3",
                                          "ADC4", "ADC5", "ADC6", "ADC7"};
constexpr unsigned int most_adc_channels = 9;
constexpr unsigned int most_pwm_outputs = sizeof(gpios) / sizeof(gpios[0]);
constexpr unsigned int most_pwm_slices = 12;

mm::mcu::AdcChannel adc_channels[most_adc_channels];
unsigned int adc_channel_count = 0;
bool adc_described = false;

mm::mcu::PwmOutput pwm_outputs[most_pwm_outputs];
unsigned int pwm_output_count = 0;
bool pwm_described = false;

// The counter the two parts share: sixteen bits, an eight-and-four divider,
// clocked from clk_sys, whose rate is read from the running board rather
// than assumed.
mm::mcu::PwmCounter pwm_counter() {
    return {mm_pico_mcu_system_clock_hz(), 16, 8, 4};
}

void describe_adc() {
    if (adc_described) return;
    const auto count = mm_pico_mcu_adc_channel_count();
    const auto base = mm_pico_mcu_adc_base_pin();
    const auto temperature = mm_pico_mcu_adc_temperature_channel();
    const auto reference = mm_pico_mcu_adc_reference_mv();
    adc_channel_count = count > most_adc_channels ? most_adc_channels : count;
    for (unsigned int channel = 0; channel < adc_channel_count; ++channel) {
        auto& entry = adc_channels[channel];
        entry.number = channel;
        entry.bits = 12;
        if (channel == temperature) {
            entry.name = "TEMP";
            entry.gpio = std::nullopt;
            entry.reference_millivolts = 0;
        } else {
            entry.name = channel < sizeof(adc_names) / sizeof(adc_names[0])
                             ? adc_names[channel]
                             : std::string_view{"ADC"};
            entry.gpio = base + channel;
            entry.reference_millivolts = reference;
        }
    }
    adc_described = true;
}

// The shortest period is two counts at divider one and the longest is 65,535
// counts at the largest divider, both as the planner would round them, so a
// period inside the advertised limits always plans.
void describe_pwm() {
    if (pwm_described) return;
    const auto count = mm_pico_mcu_gpio_count();
    pwm_output_count = count > most_pwm_outputs ? most_pwm_outputs : count;
    const auto counter = pwm_counter();
    const std::uint64_t billion = 1'000'000'000;
    const std::uint64_t shortest = (2 * billion + counter.clock_hz - 1) / counter.clock_hz;
    const std::uint64_t longest =
        (std::uint64_t{65535} * 4095 * billion) / (16 * counter.clock_hz);
    for (unsigned int pin = 0; pin < pwm_output_count; ++pin) {
        auto& entry = pwm_outputs[pin];
        entry.number = pin;
        entry.name = gpios[pin].name;
        entry.gpio = pin;
        entry.group = mm_pico_mcu_pwm_slice(pin);
        entry.comparator = mm_pico_mcu_pwm_comparator(pin);
        entry.minimum_period_ns = shortest;
        entry.maximum_period_ns = longest;
    }
    pwm_described = true;
}

// One slice's running period, kept above the ABI because the equality a
// sibling must satisfy is on the requested period, which the hardware never
// sees.
struct Slice {
    unsigned int members = 0;
    std::uint64_t requested = 0;
    mm::mcu::PwmPlan plan;
};

struct Claim {
    bool claimed = false;
    std::uint64_t requested = 0;
};

Slice slices[most_pwm_slices];
Claim claims[most_pwm_outputs];

class PicoPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Board board() const override {
        const auto count = mm_pico_mcu_gpio_count();
        if (count > sizeof(gpios) / sizeof(gpios[0])) return {};
        std::optional<mm::mcu::Led> led;
        if (mm_pico_mcu_has_led()) {
            const auto gpio = mm_pico_mcu_led_gpio();
            if (gpio < count)
                led = mm::mcu::Led{"LED", gpio, mm_pico_mcu_led_active_high() != 0};
        }
        return {mm_pico_mcu_board_name(), std::span<const mm::mcu::Gpio>{gpios, count}, led};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        return from(mm_pico_mcu_gpio_configure(pin, static_cast<int>(direction),
                                               static_cast<int>(pull)));
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        return from(mm_pico_mcu_gpio_write(pin, high ? 1 : 0));
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        int raw = 0;
        const auto status = from(mm_pico_mcu_gpio_read(pin, &raw));
        if (status == mm::mcu::Status::Ok) high = raw != 0;
        return status;
    }

    [[nodiscard]] mm::mcu::Status gpio_watch(unsigned int pin, mm::mcu::Pull pull,
                                              mm::mcu::Edge edge) override {
        return from(mm_pico_mcu_gpio_watch(pin, static_cast<int>(pull),
                                           static_cast<int>(edge)));
    }

    [[nodiscard]] mm::mcu::Status gpio_take(unsigned int pin, bool& pending) override {
        int raw = 0;
        const auto status = from(mm_pico_mcu_gpio_take(pin, &raw));
        if (status == mm::mcu::Status::Ok) pending = raw != 0;
        return status;
    }

    [[nodiscard]] mm::mcu::Status gpio_unwatch(unsigned int pin) override {
        return from(mm_pico_mcu_gpio_unwatch(pin));
    }

    [[nodiscard]] mm::mcu::Status gpio_wait(unsigned int pin,
                                             unsigned long timeout_ms,
                                             bool& pending) override {
        int raw = 0;
        const auto status = from(mm_pico_mcu_gpio_wait(pin, timeout_ms, &raw));
        if (status == mm::mcu::Status::Ok) pending = raw != 0;
        return status;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& configuration) override {
        return from(mm_pico_mcu_spi_configure(
            configuration.instance, configuration.clock_gpio, configuration.transmit_gpio,
            configuration.receive_gpio.value_or(0), configuration.receive_gpio ? 1 : 0,
            configuration.baud, static_cast<int>(configuration.mode),
            configuration.bit_order == mm::mcu::BitOrder::LeastSignificantFirst ? 1 : 0));
    }

    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int instance, std::span<const std::byte> data) override {
        return from(mm_pico_mcu_spi_write(
            instance, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
    }

    [[nodiscard]] mm::mcu::Status spi_transfer(
        unsigned int instance, std::span<const std::byte> transmit,
        std::span<std::byte> receive) override {
        if (transmit.size() != receive.size()) return mm::mcu::Status::BadArgument;
        return from(mm_pico_mcu_spi_transfer(
            instance, reinterpret_cast<const unsigned char*>(transmit.data()),
            reinterpret_cast<unsigned char*>(receive.data()), transmit.size()));
    }

    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration& configuration) override {
        return from(mm_pico_mcu_i2c_configure(configuration.instance,
                                              configuration.data_gpio,
                                              configuration.clock_gpio,
                                              configuration.baud));
    }

    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int instance, unsigned int address,
                                            std::span<const std::byte> data) override {
        return from(mm_pico_mcu_i2c_write(
            instance, address, reinterpret_cast<const unsigned char*>(data.data()),
            data.size()));
    }

    [[nodiscard]] mm::mcu::Status i2c_read(unsigned int instance, unsigned int address,
                                           std::span<std::byte> data) override {
        return from(mm_pico_mcu_i2c_read(
            instance, address, reinterpret_cast<unsigned char*>(data.data()), data.size()));
    }

    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int instance, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        return from(mm_pico_mcu_i2c_write_read(
            instance, address, reinterpret_cast<const unsigned char*>(command.data()),
            command.size(), reinterpret_cast<unsigned char*>(data.data()), data.size()));
    }

    [[nodiscard]] mm::mcu::Status uart_write(unsigned int instance, const char* text) override {
        return from(mm_pico_mcu_uart_write(instance, text));
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        return from(mm_pico_mcu_delay_ms(milliseconds));
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        unsigned long raw = 0;
        const auto status = from(mm_pico_mcu_ticks_ms(&raw));
        if (status == mm::mcu::Status::Ok) ticks = raw;
        return status;
    }

    [[nodiscard]] mm::mcu::AdcDescription adc_description() const override {
        describe_adc();
        return {std::span<const mm::mcu::AdcChannel>{adc_channels, adc_channel_count}};
    }

    [[nodiscard]] mm::mcu::Status adc_configure(unsigned int channel) override {
        return from(mm_pico_mcu_adc_configure(channel));
    }

    [[nodiscard]] mm::mcu::Status adc_read(unsigned int channel, unsigned int& count) override {
        unsigned int raw = 0;
        const auto status = from(mm_pico_mcu_adc_read(channel, &raw));
        if (status == mm::mcu::Status::Ok) count = raw;
        return status;
    }

    [[nodiscard]] mm::mcu::Status adc_release(unsigned int channel) override {
        return from(mm_pico_mcu_adc_release(channel));
    }

    [[nodiscard]] mm::mcu::PwmDescription pwm_description() const override {
        describe_pwm();
        return {std::span<const mm::mcu::PwmOutput>{pwm_outputs, pwm_output_count}};
    }

    // Validated in the contract's order -- inventory, period, this output's
    // own claim, the group -- and only then handed to the adapter, which
    // checks the pad and the comparator and records the owner last.
    [[nodiscard]] mm::mcu::Status pwm_configure(unsigned int output,
                                                std::uint64_t period_ns) override {
        describe_pwm();
        if (output >= pwm_output_count || period_ns == 0) return mm::mcu::Status::BadArgument;
        const auto& entry = pwm_outputs[output];
        if (period_ns < entry.minimum_period_ns || period_ns > entry.maximum_period_ns)
            return mm::mcu::Status::BadArgument;
        auto& claim = claims[output];
        if (claim.claimed)
            return claim.requested == period_ns ? mm::mcu::Status::Ok : mm::mcu::Status::Busy;
        if (entry.group >= most_pwm_slices) return mm::mcu::Status::Unsupported;
        auto& slice = slices[entry.group];
        mm::mcu::PwmPlan plan;
        if (slice.members != 0) {
            if (slice.requested != period_ns) return mm::mcu::Status::Busy;
            plan = slice.plan;
        } else {
            const auto planned = mm::mcu::pwm_plan(pwm_counter(), period_ns, plan);
            if (planned != mm::mcu::Status::Ok) return planned;
        }
        const auto status = from(mm_pico_mcu_pwm_configure(output, plan.top, plan.divider));
        if (status != mm::mcu::Status::Ok) return status;
        if (slice.members == 0) {
            slice.requested = period_ns;
            slice.plan = plan;
        }
        ++slice.members;
        claim.claimed = true;
        claim.requested = period_ns;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status pwm_period(unsigned int output,
                                             std::uint64_t& actual_ns) override {
        if (output >= pwm_output_count || !claims[output].claimed)
            return mm::mcu::Status::BadArgument;
        actual_ns = slices[pwm_outputs[output].group].plan.actual_period_ns;
        return mm::mcu::Status::Ok;
    }

    // The level is the duty's share of top + 1 counts, rounded to nearest,
    // so a duty equal to the actual period is top + 1: the steady high the
    // planner reserved room for.
    [[nodiscard]] mm::mcu::Status pwm_write(unsigned int output,
                                            std::uint64_t duty_ns) override {
        if (output >= pwm_output_count || !claims[output].claimed)
            return mm::mcu::Status::BadArgument;
        const auto& plan = slices[pwm_outputs[output].group].plan;
        if (duty_ns > plan.actual_period_ns) return mm::mcu::Status::BadArgument;
        const std::uint64_t counts = std::uint64_t{plan.top} + 1;
        const std::uint64_t level =
            (duty_ns * counts + plan.actual_period_ns / 2) / plan.actual_period_ns;
        return from(mm_pico_mcu_pwm_write(output, static_cast<unsigned int>(level)));
    }

    [[nodiscard]] mm::mcu::Status pwm_release(unsigned int output) override {
        if (output >= pwm_output_count) return mm::mcu::Status::BadArgument;
        auto& claim = claims[output];
        if (!claim.claimed) return mm::mcu::Status::Ok;
        const auto status = from(mm_pico_mcu_pwm_release(output));
        if (status != mm::mcu::Status::Ok) return status;
        claim = {};
        auto& slice = slices[pwm_outputs[output].group];
        if (slice.members > 0 && --slice.members == 0) slice = {};
        return mm::mcu::Status::Ok;
    }
};

PicoPlatform pico_platform;

// Registration at static initialisation. Linking this module's object is what
// makes mm.mcu answer for this platform; nothing has to reference the object,
// because objects are linked directly rather than through an archive, and the
// external CMake hand-off carries them as EXTERNAL_OBJECT for the same reason.
struct Register {
    Register() { mm::mcu::set_platform(pico_platform); }
};

const Register registered;

}  // namespace
