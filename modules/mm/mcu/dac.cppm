// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:dac;

import :status;
import :transport_types;
import :dac_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline DacDescription dac_description() {
    return platform().dac_description();
}

// Claims the output at the nearest rate it holds and holds half scale. A zero
// rate, or one outside nonzero advertised limits, is BadArgument. A pad the
// GPIO facility watches, the ADC holds, or the PWM holds is Busy; a
// PWM-backed output also claims its PWM output. The same output may be
// configured again at another rate while idle, and is Busy while started.
[[nodiscard]] inline Status dac_configure(unsigned int output, unsigned long rate_hz) {
    return platform().dac_configure(output, rate_hz);
}

[[nodiscard]] inline Status dac_rate(unsigned int output, Frequency& actual) {
    return platform().dac_rate(output, actual);
}

// Starts playing from the queue. Progress restarts at zero.
[[nodiscard]] inline Status dac_start(unsigned int output) {
    return platform().dac_start(output);
}

// Queues levels, oldest first. accepted is written on Ok and may be less than
// the span, including zero. With nothing queued at a period the output holds
// half scale -- silence, not the last level -- and the period counts as
// missed; the transport does that itself, without waiting for a caller.
[[nodiscard]] inline Status dac_give(unsigned int output,
                                     std::span<const std::uint16_t> levels,
                                     std::size_t& accepted) {
    return platform().dac_give(output, levels, accepted);
}

[[nodiscard]] inline Status dac_progress(unsigned int output, Progress& progress) {
    return platform().dac_progress(output, progress);
}

// Discards the queue and returns to half scale. The output stays claimed.
[[nodiscard]] inline Status dac_stop(unsigned int output) {
    return platform().dac_stop(output);
}

[[nodiscard]] inline Status dac_release(unsigned int output) {
    return platform().dac_release(output);
}

// The first output attached to gpio, or BadArgument with output untouched.
[[nodiscard]] Status dac_output_for_gpio(unsigned int gpio, unsigned int& output);

}
