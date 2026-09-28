// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

module mm.audio.microphone;

namespace mm::audio::microphone {

mm::audio::Status Microphone::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::audio::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::audio::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::audio::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::audio::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::audio::Status::Timeout;
        case mm::mcu::Status::TransportError: return mm::audio::Status::TransportError;
    }
    return mm::audio::Status::TransportError;
}

mm::audio::Description Microphone::description() const {
    return {"microphone", wiring_.rate, 1, mm::audio::Direction::Input};
}

mm::audio::Status Microphone::initialize() {
    if (wiring_.rate == 0 || wiring_.bits < 1 || wiring_.bits > 16)
        return mm::audio::Status::BadArgument;

    // The ADC is the authority on whether the channel exists; a channel it does
    // not offer answers BadArgument, which the driver forwards.
    auto status = from_mcu(mm::mcu::adc_configure(wiring_.channel));
    if (status != mm::audio::Status::Ok) return status;

    ready_ = true;
    return mm::audio::Status::Ok;
}

mm::audio::Status Microphone::capture(std::span<mm::audio::Sample> samples,
                                      std::size_t& count) {
    if (!ready_) return mm::audio::Status::NotInitialized;
    if (samples.empty()) return mm::audio::Status::BadArgument;

    // A microphone's output is centred on half-scale, so a quiet input is a
    // count near the middle of the ADC's range. Centring puts a quiet input at
    // a sample near zero, and the shift scales the result to sixteen bits.
    const auto half = 1u << (wiring_.bits - 1);
    const auto shift = 16u - wiring_.bits;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        unsigned int raw = 0;
        auto status = from_mcu(mm::mcu::adc_read(wiring_.channel, raw));
        if (status != mm::audio::Status::Ok) return status;
        const auto centred =
            static_cast<std::int32_t>(raw) - static_cast<std::int32_t>(half);
        samples[i] = static_cast<mm::audio::Sample>(
            shift == 0 ? centred : centred << shift);
    }

    count = samples.size();
    return mm::audio::Status::Ok;
}

mm::audio::Status Microphone::shutdown() {
    if (!ready_) return mm::audio::Status::NotInitialized;
    auto status = from_mcu(mm::mcu::adc_release(wiring_.channel));
    if (status != mm::audio::Status::Ok) return status;
    ready_ = false;
    return mm::audio::Status::Ok;
}

}  // namespace mm::audio::microphone
