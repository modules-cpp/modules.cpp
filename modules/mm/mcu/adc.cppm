// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:adc;

import :status;
import :adc_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline AdcDescription adc_description() {
    return platform().adc_description();
}

// Claims a channel: a pin channel's pad goes to the converter, an internal
// source is enabled. A pad the GPIO facility merely configured is taken over;
// one it watches, or the PWM holds, answers Busy. Idempotent once claimed.
[[nodiscard]] inline Status adc_configure(unsigned int channel) {
    return platform().adc_configure(channel);
}

// One bounded conversion. count changes only on Ok, which is the platform's
// obligation; a channel not configured answers BadArgument.
[[nodiscard]] inline Status adc_read(unsigned int channel, unsigned int& count) {
    return platform().adc_read(channel, count);
}

[[nodiscard]] inline Status adc_release(unsigned int channel) {
    return platform().adc_release(channel);
}

// The first channel attached to gpio, or BadArgument with channel untouched.
[[nodiscard]] Status adc_channel_for_gpio(unsigned int gpio, unsigned int& channel);

// count * reference / (2^bits - 1), rounded to nearest, in integer arithmetic;
// zero when the channel has no reference, its bits are outside [1, 31], or the
// count is outside the channel's range.
[[nodiscard]] unsigned int adc_millivolts(const AdcChannel& channel, unsigned int count);

}
