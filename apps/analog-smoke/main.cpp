// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <cstdint>
#include <span>

import mm.mcu;
import mm.stdio;

namespace {

// GP16 drives the filter into GP26. GP17 shares GP16's counter and GP0 its
// comparator; both stay unconnected.
constexpr unsigned int driver_pin = 16;
constexpr unsigned int sibling_pin = 17;
constexpr unsigned int alias_pin = 0;
constexpr unsigned int sensor_pin = 26;
constexpr std::uint64_t period_ns = 1'000'000;
constexpr unsigned int settle_ms = 100;   // ten time constants of 10 k and 1 uF
constexpr unsigned int samples = 16;

using mm::mcu::Direction;
using mm::mcu::Pull;
using mm::mcu::Status;

void say(const char* message) {
    auto& console = mm::stdio::selected_console();
    const char* end = message;
    while (*end) ++end;
    auto bytes = std::as_bytes(std::span{message, static_cast<std::size_t>(end - message)});
    while (!bytes.empty()) {
        std::size_t written = 0;
        if (console.write(bytes, written) != mm::stdio::Status::Ok || written == 0) break;
        bytes = bytes.subspan(written);
    }
    (void)console.flush();
}

void say_number(unsigned long value) {
    char text[24];
    std::size_t at = sizeof(text);
    text[--at] = '\0';
    do {
        text[--at] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    say(text + at);
}

unsigned int channel = 0;
unsigned int output = 0;
unsigned int sibling = 0;
unsigned int alias = 0;
bool have_sibling = false;
bool have_alias = false;

void release_everything() {
    (void)mm::mcu::pwm_release(output);
    if (have_sibling) (void)mm::mcu::pwm_release(sibling);
    if (have_alias) (void)mm::mcu::pwm_release(alias);
    (void)mm::mcu::adc_release(channel);
}

int fail(int code, const char* message) {
    say(message);
    release_everything();
    return code;
}

// The filtered level, averaged over a few conversions once the capacitor has
// settled.
bool average_count(unsigned long& average) {
    if (mm::mcu::delay_ms(settle_ms) != Status::Ok) return false;
    unsigned long sum = 0;
    for (unsigned int i = 0; i < samples; ++i) {
        unsigned int count = 0;
        if (mm::mcu::adc_read(channel, count) != Status::Ok) return false;
        if (count > 4095) return false;
        sum += count;
    }
    average = sum / samples;
    return true;
}

bool ratio_holds(std::uint64_t actual, unsigned int numerator, unsigned int denominator,
                 unsigned long& measured) {
    if (mm::mcu::pwm_write(output, actual * numerator / denominator) != Status::Ok) return false;
    if (!average_count(measured)) return false;
    const unsigned long expected = 4095ul * numerator / denominator;
    const unsigned long tolerance = 410;
    return measured + tolerance >= expected && measured <= expected + tolerance;
}

} // namespace

int main() {
    (void)mm::stdio::selected_console().initialize();

    // Availability is decided before anything is driven.
    if (mm::mcu::adc_channel_for_gpio(sensor_pin, channel) != Status::Ok ||
        mm::mcu::pwm_output_for_gpio(driver_pin, output) != Status::Ok) {
        say("Analog facilities not in the inventory; skip\n");
        return 0;
    }
    have_sibling = mm::mcu::pwm_output_for_gpio(sibling_pin, sibling) == Status::Ok;
    have_alias = mm::mcu::pwm_output_for_gpio(alias_pin, alias) == Status::Ok;

    const auto adc_available = mm::mcu::adc_configure(channel);
    if (adc_available == Status::Unsupported) {
        say("ADC unavailable; skip\n");
        return 0;
    }
    if (adc_available != Status::Ok) return fail(1, "ADC configure failed\n");
    const auto pwm_available = mm::mcu::pwm_configure(output, period_ns);
    if (pwm_available == Status::Unsupported) {
        say("PWM unavailable; skip\n");
        release_everything();
        return 0;
    }
    if (pwm_available != Status::Ok) return fail(2, "PWM configure failed\n");
    std::uint64_t actual = 0;
    if (mm::mcu::pwm_period(output, actual) != Status::Ok || actual == 0)
        return fail(3, "PWM period read failed\n");
    say("PWM period ns: ");
    say_number(static_cast<unsigned long>(actual));
    say("\n");

    // The rules that need no wire.
    if (have_sibling) {
        if (mm::mcu::pwm_configure(sibling, period_ns * 2) != Status::Busy)
            return fail(4, "a sibling at another period was not refused\n");
        if (mm::mcu::pwm_configure(sibling, period_ns) != Status::Ok)
            return fail(5, "a sibling at the same period could not join\n");
    }
    if (have_alias && mm::mcu::pwm_configure(alias, period_ns) != Status::Busy)
        return fail(6, "an alias of a claimed comparator was not refused\n");
    if (mm::mcu::gpio_configure(driver_pin, Direction::Out, Pull::None) != Status::Busy)
        return fail(7, "the PWM pin was not refused to GPIO\n");
    if (mm::mcu::gpio_configure(sensor_pin, Direction::In, Pull::None) != Status::Busy)
        return fail(8, "the ADC pin was not refused to GPIO\n");
    std::uint64_t sentinel = 7;
    if (mm::mcu::pwm_write(output, actual + 1) != Status::BadArgument ||
        mm::mcu::pwm_period(999, sentinel) != Status::BadArgument || sentinel != 7)
        return fail(9, "a refused call was accepted or changed its output\n");

    // The wire.
    unsigned long measured = 0;
    if (!average_count(measured)) return fail(10, "raw count out of range\n");
    if (!ratio_holds(actual, 1, 4, measured)) {
        say("quarter duty read ");
        say_number(measured);
        return fail(11, "; expected near 1024\n");
    }
    if (!ratio_holds(actual, 1, 2, measured)) {
        say("half duty read ");
        say_number(measured);
        return fail(12, "; expected near 2048\n");
    }
    if (!ratio_holds(actual, 3, 4, measured)) {
        say("three-quarter duty read ");
        say_number(measured);
        return fail(13, "; expected near 3071\n");
    }

    // The internal channel reads; what its count means is the part's business.
    unsigned int temperature_channel = 0;
    bool found = false;
    for (const auto& entry : mm::mcu::adc_description().channels) {
        if (!entry.gpio) {
            temperature_channel = entry.number;
            found = true;
            break;
        }
    }
    if (found) {
        unsigned int count = 0;
        if (mm::mcu::adc_configure(temperature_channel) != Status::Ok ||
            mm::mcu::adc_read(temperature_channel, count) != Status::Ok ||
            mm::mcu::adc_release(temperature_channel) != Status::Ok)
            return fail(14, "the temperature channel could not be read\n");
        say("temperature channel raw count: ");
        say_number(count);
        say("\n");
    }

    if (mm::mcu::pwm_release(output) != Status::Ok ||
        (have_sibling && mm::mcu::pwm_release(sibling) != Status::Ok) ||
        mm::mcu::adc_release(channel) != Status::Ok)
        return fail(15, "release failed\n");
    if (mm::mcu::gpio_configure(driver_pin, Direction::In, Pull::None) != Status::Ok ||
        mm::mcu::gpio_configure(sensor_pin, Direction::In, Pull::None) != Status::Ok)
        return fail(16, "the pins did not return to GPIO\n");
    say("Analog smoke passed.\n");
    return 0;
}
