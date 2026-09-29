// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:adc;

import :status;
import :transport_types;
import :adc_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline AdcDescription adc_description() {
    return platform().adc_description();
}

// Claims a channel: a pin channel's pad goes to the converter, an internal
// source is enabled. A pad the GPIO facility merely configured is taken over;
// one it watches, or the PWM holds, answers Busy. Idempotent once claimed.
// While any channel is paced, another channel answers Busy.
[[nodiscard]] inline Status adc_configure(unsigned int channel) {
    return platform().adc_configure(channel);
}

// One bounded conversion. count changes only on Ok, which is the platform's
// obligation; a channel not configured answers BadArgument. While any channel
// is paced, every channel answers Busy: the converter has one multiplexer, and
// switching it during capture would corrupt the paced stream.
[[nodiscard]] inline Status adc_read(unsigned int channel, unsigned int& count) {
    return platform().adc_read(channel, count);
}

// Returns the pad, and ends pacing when the channel was paced.
[[nodiscard]] inline Status adc_release(unsigned int channel) {
    return platform().adc_release(channel);
}

// Paced capture. adc_pace claims the channel as adc_configure does, and the
// whole converter with it, at the nearest rate the converter holds; the
// capture idles until adc_pace_start. A zero rate, one above the channel's
// maximum_pace_hz, or a channel the platform cannot pace is BadArgument;
// another paced channel is Busy. The same channel may be paced again at
// another rate while idle, and is Busy while started.
[[nodiscard]] inline Status adc_pace(unsigned int channel, unsigned long rate_hz) {
    return platform().adc_pace(channel, rate_hz);
}

[[nodiscard]] inline Status adc_pace_rate(unsigned int channel, Frequency& actual) {
    return platform().adc_pace_rate(channel, actual);
}

// Discards whatever the platform buffered and starts converting. Progress
// restarts at zero, and nothing converted before start is ever taken.
[[nodiscard]] inline Status adc_pace_start(unsigned int channel) {
    return platform().adc_pace_start(channel);
}

// Counts converted since the last take, oldest first. count is written on Ok
// and may be zero, which is an answer rather than a failure.
[[nodiscard]] inline Status adc_take(unsigned int channel, std::span<std::uint16_t> counts,
                                     std::size_t& count) {
    return platform().adc_take(channel, counts, count);
}

[[nodiscard]] inline Status adc_pace_progress(unsigned int channel, Progress& progress) {
    return platform().adc_pace_progress(channel, progress);
}

// Stops converting and discards what the platform buffered. The channel stays
// paced, and the converter stays owned, until adc_release.
[[nodiscard]] inline Status adc_pace_stop(unsigned int channel) {
    return platform().adc_pace_stop(channel);
}

// The first channel attached to gpio, or BadArgument with channel untouched.
[[nodiscard]] Status adc_channel_for_gpio(unsigned int gpio, unsigned int& channel);

// count * reference / (2^bits - 1), rounded to nearest, in integer arithmetic;
// zero when the channel has no reference, its bits are outside [1, 31], or the
// count is outside the channel's range.
[[nodiscard]] unsigned int adc_millivolts(const AdcChannel& channel, unsigned int count);

}
