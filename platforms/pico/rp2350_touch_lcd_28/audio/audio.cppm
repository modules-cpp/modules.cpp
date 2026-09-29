// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

export module platform.rp2350_touch_lcd_28.audio;

import mm.audio;
import mm.mcu;

// Module linkage rather than an unnamed namespace: the two device classes have
// virtual functions, and docs/modules-c++20.mdy says why that matters.
namespace platform::rp2350_touch_lcd_28::audio_provider {

// The rates the PCM5101A's PLL accepts from a bit clock of 32 times the rate,
// per its datasheet's table of PLL operation, less 384 kHz.
constexpr unsigned int rates_hz[] = {32'000, 44'100, 48'000, 96'000, 192'000};

// Frames service moves in one pass, and the most it holds pending.
constexpr std::size_t scratch_frames = 64;

mm::mcu::I2sConfiguration link(unsigned int rate_hz) {
    mm::mcu::I2sConfiguration configuration;
    configuration.instance = 0;
    configuration.bit_clock_gpio = 2;
    configuration.word_clock_gpio = 3;
    configuration.transmit_gpio = 4;
    configuration.rate_hz = rate_hz;
    configuration.slot_bits = 16;
    return configuration;
}

mm::audio::Status from_mcu(mm::mcu::Status status) {
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

// The member of rates_hz nearest to requested.
unsigned int nearest_rate(unsigned int requested) {
    unsigned int best = rates_hz[0];
    for (const auto rate : rates_hz) {
        const auto distance = rate > requested ? rate - requested : requested - rate;
        const auto best_distance = best > requested ? best - requested : requested - best;
        if (distance < best_distance) best = rate;
    }
    return best;
}

// The nearest whole hertz to an exact rate.
unsigned int nearest_hz(const mm::mcu::Frequency& frequency) {
    if (frequency.denominator == 0) return 0;
    return static_cast<unsigned int>((frequency.numerator + frequency.denominator / 2) /
                                     frequency.denominator);
}

class Speaker final : public mm::audio::Out {
public:
    Speaker() = default;
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;
    Speaker(Speaker&&) = delete;
    Speaker& operator=(Speaker&&) = delete;
    ~Speaker() override = default;

    // The depth is the Speaker's own scratch: the platform's I2S buffer is
    // deeper still, and servicing within the shorter time is always enough.
    [[nodiscard]] mm::audio::Description description() const override {
        return {"pcm5101a", rates_hz[0], rates_hz[std::size(rates_hz) - 1], scratch_frames};
    }

    // The DAC has no control port, so there is nothing to bring up.
    [[nodiscard]] mm::audio::Status initialize() override {
        if (state_ == State::Idle) state_ = State::Initialized;
        return mm::audio::Status::Ok;
    }

    // Starts the link at the nearest rate the DAC's PLL accepts. Another rate
    // while idle releases the link and starts it afresh; the DAC relocks.
    [[nodiscard]] mm::audio::Status configure(const mm::audio::Format& requested,
                                              mm::audio::Format& actual) override {
        if (state_ == State::Idle) return mm::audio::Status::NotInitialized;
        if (state_ == State::Started) return mm::audio::Status::Busy;
        const auto rate = nearest_rate(requested.rate_hz);
        if (state_ == State::Configured && rate != configured_hz_) {
            const auto released = from_mcu(mm::mcu::i2s_release(0));
            if (released != mm::audio::Status::Ok) return released;
            state_ = State::Initialized;
        }
        auto status = from_mcu(mm::mcu::i2s_configure(link(rate)));
        if (status != mm::audio::Status::Ok) return status;
        mm::mcu::Frequency running;
        status = from_mcu(mm::mcu::i2s_rate(0, running));
        if (status != mm::audio::Status::Ok) return status;
        configured_hz_ = rate;
        format_ = {.rate_hz = nearest_hz(running)};
        state_ = State::Configured;
        actual = format_;
        return mm::audio::Status::Ok;
    }

    [[nodiscard]] mm::audio::Status rate(mm::audio::Rate& actual) const override {
        if (state_ != State::Configured && state_ != State::Started)
            return mm::audio::Status::NotInitialized;
        mm::mcu::Frequency running;
        const auto status = from_mcu(mm::mcu::i2s_rate(0, running));
        if (status == mm::audio::Status::Ok) actual = {running.numerator, running.denominator};
        return status;
    }

    [[nodiscard]] mm::audio::Status start(mm::audio::Stream& stream) override {
        if (state_ == State::Started) return mm::audio::Status::Busy;
        if (state_ != State::Configured) return mm::audio::Status::NotInitialized;
        if (stream.format() != format_) return mm::audio::Status::BadArgument;
        auto status = stream.claim(mm::audio::End::Consumer);
        if (status != mm::audio::Status::Ok) return status;
        status = from_mcu(mm::mcu::i2s_start(0, mm::mcu::I2sDirection::Transmit));
        if (status != mm::audio::Status::Ok) {
            stream.release(mm::audio::End::Consumer);
            return status;
        }
        stream_ = &stream;
        pending_first_ = 0;
        pending_count_ = 0;
        reported_missed_ = 0;
        state_ = State::Started;
        return mm::audio::Status::Ok;
    }

    // The pending run first; the Stream only once nothing is pending.
    [[nodiscard]] mm::audio::Status service() override {
        if (state_ != State::Started) return mm::audio::Status::NotInitialized;
        for (;;) {
            if (pending_count_ == 0) {
                const auto status = fill_pending();
                if (status != mm::audio::Status::Ok) return status;
                if (pending_count_ == 0) break;
            }
            const auto status = offer_pending();
            if (status != mm::audio::Status::Ok) return status;
            if (pending_count_ != 0) break;
        }
        return report_underrun();
    }

    [[nodiscard]] mm::audio::Status position(std::uint64_t& samples) const override {
        if (state_ != State::Started) return mm::audio::Status::NotInitialized;
        mm::mcu::Progress progress;
        const auto status = transmit_progress(progress);
        if (status == mm::audio::Status::Ok) samples = progress.completed;
        return status;
    }

    [[nodiscard]] mm::audio::Status pending(std::size_t& samples) const override {
        if (state_ != State::Started) return mm::audio::Status::NotInitialized;
        mm::mcu::Progress progress;
        const auto status = transmit_progress(progress);
        if (status == mm::audio::Status::Ok) samples = pending_count_ + progress.queued;
        return status;
    }

    // The transmitter returns to zeros; the clocks keep running, so the DAC
    // stays locked for the next start.
    [[nodiscard]] mm::audio::Status stop() override {
        if (state_ != State::Started) return mm::audio::Status::NotInitialized;
        const auto status =
            from_mcu(mm::mcu::i2s_stop(0, mm::mcu::I2sDirection::Transmit));
        if (status != mm::audio::Status::Ok) return status;
        stream_->release(mm::audio::End::Consumer);
        stream_ = nullptr;
        pending_first_ = 0;
        pending_count_ = 0;
        state_ = State::Configured;
        return mm::audio::Status::Ok;
    }

    // There is no mute to set: a sleeping Speaker is a stopped one, sending
    // zeros.
    [[nodiscard]] mm::audio::Status sleep() override {
        if (state_ == State::Idle) return mm::audio::Status::NotInitialized;
        if (state_ == State::Started) return stop();
        return mm::audio::Status::Ok;
    }

private:
    enum class State { Idle, Initialized, Configured, Started };

    [[nodiscard]] mm::audio::Status transmit_progress(mm::mcu::Progress& progress) const {
        return from_mcu(mm::mcu::i2s_progress(0, mm::mcu::I2sDirection::Transmit, progress));
    }

    // Read from the Stream only into an empty run, each sample in both slots.
    [[nodiscard]] mm::audio::Status fill_pending() {
        std::int16_t samples[scratch_frames];
        std::size_t count = 0;
        const auto status = stream_->read(samples, count);
        if (status != mm::audio::Status::Ok) return status;
        for (std::size_t i = 0; i < count; ++i) {
            words_[2 * i] = samples[i];
            words_[2 * i + 1] = samples[i];
        }
        pending_first_ = 0;
        pending_count_ = count;
        return mm::audio::Status::Ok;
    }

    // Whatever the transmitter does not accept stays pending.
    [[nodiscard]] mm::audio::Status offer_pending() {
        std::size_t accepted = 0;
        const auto status = from_mcu(mm::mcu::i2s_write(
            0,
            std::span<const std::int16_t>{words_}.subspan(2 * pending_first_,
                                                          2 * pending_count_),
            accepted));
        if (status != mm::audio::Status::Ok) return status;
        pending_first_ += accepted;
        pending_count_ -= accepted;
        if (pending_count_ == 0) pending_first_ = 0;
        return mm::audio::Status::Ok;
    }

    [[nodiscard]] mm::audio::Status report_underrun() {
        mm::mcu::Progress progress;
        const auto status = transmit_progress(progress);
        if (status != mm::audio::Status::Ok) return status;
        if (progress.missed != reported_missed_) {
            stream_->underran(progress.missed - reported_missed_);
            reported_missed_ = progress.missed;
        }
        return mm::audio::Status::Ok;
    }

    State state_ = State::Idle;
    unsigned int configured_hz_ = 0;
    mm::audio::Format format_;
    mm::audio::Stream* stream_ = nullptr;
    std::uint32_t reported_missed_ = 0;
    std::int16_t words_[2 * scratch_frames] = {};
    std::size_t pending_first_ = 0;
    std::size_t pending_count_ = 0;
};

// The board has no microphone: every call answers Unsupported, from the board
// rather than from mm.audio's fallback.
class Microphone final : public mm::audio::In {};

Speaker speaker;
Microphone microphone;

struct Register {
    Register() {
        mm::audio::set_out(speaker);
        mm::audio::set_in(microphone);
    }
};

const Register registered;

}  // namespace platform::rp2350_touch_lcd_28::audio_provider
