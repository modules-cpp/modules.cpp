// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The analog facilities' code: the two lookups, the millivolt conversion, and
// the counter planner. All of it is arithmetic over the descriptions, none of
// it touches hardware, and all of it runs on the host under test.
module;

#include <cstdint>
#include <span>
#include <limits>

module mm.mcu;

namespace mm::mcu {

namespace {

constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;

// Floor of a * b / c with the remainder, without a 128-bit intermediate: the
// product is fed to a long division one byte of a at a time, so the running
// remainder never exceeds c * 256 + 255 * b. The caller keeps c and b small
// enough for that to fit, and the quotient is checked as it grows.
bool multiply_divide(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                     std::uint64_t& quotient, std::uint64_t& remainder) {
    if (c == 0) return false;
    std::uint64_t q = 0;
    std::uint64_t r = 0;
    for (int shift = 56; shift >= 0; shift -= 8) {
        const std::uint64_t byte = (a >> shift) & 0xff;
        const std::uint64_t current = r * 256 + byte * b;
        const std::uint64_t digit = current / c;
        r = current % c;
        if (q > (std::numeric_limits<std::uint64_t>::max() - digit) / 256) return false;
        q = q * 256 + digit;
    }
    quotient = q;
    remainder = r;
    return true;
}

// Round to nearest, a half rounding up.
bool multiply_divide_round(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                           std::uint64_t& result) {
    std::uint64_t q = 0;
    std::uint64_t r = 0;
    if (!multiply_divide(a, b, c, q, r)) return false;
    if (2 * r >= c) {
        if (q == std::numeric_limits<std::uint64_t>::max()) return false;
        ++q;
    }
    result = q;
    return true;
}

}  // namespace

Status adc_channel_for_gpio(unsigned int gpio, unsigned int& channel) {
    for (const auto& entry : platform().adc_description().channels) {
        if (entry.gpio && *entry.gpio == gpio) {
            channel = entry.number;
            return Status::Ok;
        }
    }
    return Status::BadArgument;
}

unsigned int adc_millivolts(const AdcChannel& channel, unsigned int count) {
    if (channel.reference_millivolts == 0 || channel.bits < 1 || channel.bits > 31)
        return 0;
    const std::uint64_t full_scale = (std::uint64_t{1} << channel.bits) - 1;
    if (count > full_scale) return 0;
    // count < 2^31 and the reference < 2^32, so the product is under 2^63 and
    // the rounding term fits beside it.
    const std::uint64_t product =
        std::uint64_t{count} * channel.reference_millivolts + full_scale / 2;
    return static_cast<unsigned int>(product / full_scale);
}

Status pwm_output_for_gpio(unsigned int gpio, unsigned int& output) {
    for (const auto& entry : platform().pwm_description().outputs) {
        if (entry.gpio && *entry.gpio == gpio) {
            output = entry.number;
            return Status::Ok;
        }
    }
    return Status::BadArgument;
}

// The bounds are what keep the byte-wise division's intermediates inside 64
// bits: a divider of at most 24 bits keeps divider * 1e9 under 2^54, and a
// clock of at most 2^32 keeps 2^fraction * clock under 2^48. Both are far
// beyond any counter this shape describes.
Status pwm_plan(const PwmCounter& counter, std::uint64_t period_ns, PwmPlan& out) {
    if (counter.clock_hz == 0 || counter.clock_hz > (std::uint64_t{1} << 32) ||
        period_ns == 0)
        return Status::BadArgument;
    if (counter.counter_bits < 2 || counter.counter_bits > 32 ||
        counter.divider_integer_bits < 1 || counter.divider_fraction_bits > 16 ||
        counter.divider_integer_bits + counter.divider_fraction_bits > 24)
        return Status::BadArgument;

    const std::uint64_t one = std::uint64_t{1} << counter.divider_fraction_bits;
    const std::uint64_t divider_limit =
        (std::uint64_t{1} << (counter.divider_integer_bits + counter.divider_fraction_bits)) -
        1;
    const std::uint64_t most_counts = (std::uint64_t{1} << counter.counter_bits) - 1;

    // period * clock is the period in ticks scaled by 1e9; it must fit before
    // anything else is asked of it.
    if (period_ns > std::numeric_limits<std::uint64_t>::max() / counter.clock_hz)
        return Status::BadArgument;
    const std::uint64_t scaled_ticks = period_ns * counter.clock_hz;

    // Counts at a divider, rounded to nearest: scaled_ticks * one / (divider * 1e9).
    const auto counts_at = [&](std::uint64_t divider, std::uint64_t& counts) {
        return multiply_divide_round(scaled_ticks, one, divider * nanoseconds_per_second,
                                     counts);
    };

    std::uint64_t at_one = 0;
    std::uint64_t at_limit = 0;
    if (!counts_at(one, at_one) || !counts_at(divider_limit, at_limit))
        return Status::BadArgument;
    if (at_one < 2 || at_limit > most_counts) return Status::BadArgument;

    // Counts fall as the divider grows, so the smallest fitting divider is
    // found by bisection over [one, divider_limit].
    std::uint64_t low = one;
    std::uint64_t high = divider_limit;
    while (low < high) {
        const std::uint64_t middle = low + (high - low) / 2;
        std::uint64_t counts = 0;
        if (!counts_at(middle, counts)) return Status::BadArgument;
        if (counts <= most_counts)
            high = middle;
        else
            low = middle + 1;
    }
    std::uint64_t counts = 0;
    if (!counts_at(low, counts)) return Status::BadArgument;
    if (counts < 2 || counts > most_counts) return Status::BadArgument;

    // What the counter will run: (top + 1) * divider * 1e9 / (one * clock).
    std::uint64_t actual = 0;
    if (!multiply_divide_round(counts * low, nanoseconds_per_second, one * counter.clock_hz,
                               actual))
        return Status::BadArgument;

    out.top = static_cast<unsigned int>(counts - 1);
    out.divider = static_cast<unsigned int>(low);
    out.actual_period_ns = actual;
    return Status::Ok;
}

}  // namespace mm::mcu
