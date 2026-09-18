// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:pwm;

import :status;
import :pwm_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline PwmDescription pwm_description() {
    return platform().pwm_description();
}

// Claims an output at the nearest period the hardware holds and starts it at
// zero duty. A zero period, or one outside nonzero advertised limits, is
// BadArgument. A sibling in the group at another requested period, an alias of
// a claimed comparator, a watched pad, or a pad the ADC holds is Busy. The
// same output at the same requested period is idempotent.
[[nodiscard]] inline Status pwm_configure(unsigned int output, std::uint64_t period_ns) {
    return platform().pwm_configure(output, period_ns);
}

// The period actually running, which is what a duty is measured against.
[[nodiscard]] inline Status pwm_period(unsigned int output, std::uint64_t& actual_ns) {
    return platform().pwm_period(output, actual_ns);
}

// Zero is a steady low, the actual period a steady high, more is BadArgument.
[[nodiscard]] inline Status pwm_write(unsigned int output, std::uint64_t duty_ns) {
    return platform().pwm_write(output, duty_ns);
}

[[nodiscard]] inline Status pwm_release(unsigned int output) {
    return platform().pwm_release(output);
}

// The first output attached to gpio, or BadArgument with output untouched.
[[nodiscard]] Status pwm_output_for_gpio(unsigned int gpio, unsigned int& output);

// Chooses the smallest divider at which period_ns fits the counter, so the
// top is as large as it can be and the duty has the most steps, then the
// nearest top. A divider fits when the period, in counts at that divider
// rounded to nearest with a half rounding up, is at most 2^counter_bits - 1:
// top is never more than 2^counter_bits - 2, so that top + 1, the level that
// is a steady high, fits the compare register. BadArgument for a zero clock,
// a bit width outside what the arithmetic holds, a period under two ticks or
// beyond the counter's reach, or an intermediate that would overflow. out
// changes only on Ok.
[[nodiscard]] Status pwm_plan(const PwmCounter& counter, std::uint64_t period_ns,
                              PwmPlan& out);

}
