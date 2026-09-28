// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Register values follow the ES8311 datasheet's map as Espressif's own driver
// programs it for a slave-mode codec with the master clock taken from the bit
// clock. They are unqualified on hardware until a board with this codec plays
// and records through them.
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

module mm.audio.es8311;

namespace mm::audio::es8311 {

namespace {

using mm::audio::Status;

// Eight-bit register addresses, one byte on the wire.
constexpr unsigned int reset_register = 0x00;
constexpr unsigned int clock_manager = 0x01;     // clock source and enables
constexpr unsigned int clock_multiplier = 0x02;  // pre-divider and multiplier
constexpr unsigned int clock_adc_osr = 0x03;     // speed mode and ADC oversampling
constexpr unsigned int clock_dac_osr = 0x04;     // DAC oversampling
constexpr unsigned int clock_dividers = 0x05;    // ADC and DAC clock dividers
constexpr unsigned int serial_in = 0x09;         // the DAC's serial port, SDP IN
constexpr unsigned int serial_out = 0x0a;        // the ADC's serial port, SDP OUT
constexpr unsigned int system_power_1 = 0x0b;
constexpr unsigned int system_power_2 = 0x0c;
constexpr unsigned int analog_power = 0x0d;
constexpr unsigned int analog_bias = 0x0e;
constexpr unsigned int system_reference_1 = 0x10;
constexpr unsigned int system_reference_2 = 0x11;
constexpr unsigned int dac_power = 0x12;
constexpr unsigned int output_driver = 0x13;
constexpr unsigned int microphone_select = 0x14;  // input select and PGA gain
constexpr unsigned int adc_ramp = 0x15;
constexpr unsigned int adc_scale = 0x16;
constexpr unsigned int adc_volume = 0x17;
constexpr unsigned int adc_high_pass = 0x1b;
constexpr unsigned int adc_equaliser = 0x1c;
constexpr unsigned int dac_volume = 0x32;
constexpr unsigned int dac_ramp = 0x37;
constexpr unsigned int chip_id_1 = 0xfd;
constexpr unsigned int chip_id_2 = 0xfe;

// What the part answers in its two identification registers.
constexpr unsigned int expected_id_1 = 0x83;
constexpr unsigned int expected_id_2 = 0x11;

// Reset: every block held in reset, then released with the state machine on
// and the codec a slave, which is what bit six clear means.
constexpr unsigned int reset_enter = 0x1f;
constexpr unsigned int reset_leave = 0x80;

// The master clock from the bit clock (bit seven) with every clock enabled.
constexpr unsigned int clocks_from_bit_clock = 0xbf;

// The internal master clock is 256 times the rate. Sixteen-bit slots make the
// bit clock 32 times the rate, so the multiplier is eight; thirty-two-bit
// slots make it 64 times, so four. Pre-divider one either way.
constexpr unsigned int multiply_by_8 = 0x18;
constexpr unsigned int multiply_by_4 = 0x10;
constexpr unsigned int single_speed_osr = 0x10;
constexpr unsigned int dividers_one = 0x00;

// Either serial port: I2S framing (bits one and zero clear), the word length
// in bits four to two -- three for sixteen bits, four for thirty-two -- and
// the mute bit.
constexpr unsigned int word_16 = 0x0c;
constexpr unsigned int word_32 = 0x10;
constexpr unsigned int serial_mute = 0x40;

constexpr unsigned int zero_decibels = 0xbf;

// The analog microphone input, MIC1P and MIC1N, selected with bit four; the
// PGA's gain in the low four bits.
constexpr unsigned int microphone_input = 0x10;
constexpr unsigned int most_gain = 10;

// What both directions share after reset: the clock manager, the system and
// reference registers as the vendor sequence sets them, and the analog
// section, bias included.
constexpr unsigned char shared_power_up[][2] = {
    {clock_manager, clocks_from_bit_clock},
    {system_power_1, 0x00},
    {system_power_2, 0x00},
    {system_reference_1, 0x1f},
    {system_reference_2, 0x7f},
    {analog_power, 0x01},
    {analog_bias, 0x02},
};

// The DAC's: its power, the output driver, the ramp with the equaliser
// bypassed, and unity volume.
constexpr unsigned char dac_power_up[][2] = {
    {dac_power, 0x00},
    {output_driver, 0x10},
    {dac_ramp, 0x08},
    {dac_volume, zero_decibels},
};

// The ADC's after the input select: its ramp, its digital scale, unity
// volume, and the high-pass filter that removes the capsule's DC bias with
// the equaliser bypassed.
constexpr unsigned char adc_power_up[][2] = {
    {adc_ramp, 0x40},
    {adc_scale, 0x24},
    {adc_volume, zero_decibels},
    {adc_high_pass, 0x0a},
    {adc_equaliser, 0x6a},
};

// The rates the codec's single- and double-speed modes cover.
constexpr unsigned int slowest_hz = 8'000;
constexpr unsigned int fastest_hz = 96'000;

Status from_mcu(mm::mcu::Status status) {
    switch (status) {
        case mm::mcu::Status::Ok: return Status::Ok;
        case mm::mcu::Status::BadArgument: return Status::BadArgument;
        case mm::mcu::Status::Unsupported: return Status::Unsupported;
        case mm::mcu::Status::Busy: return Status::Busy;
        case mm::mcu::Status::Timeout: return Status::Timeout;
        case mm::mcu::Status::TransportError: return Status::TransportError;
    }
    return Status::TransportError;
}

// The nearest whole hertz to an exact rate.
unsigned int nearest_hz(const mm::mcu::Frequency& frequency) {
    if (frequency.denominator == 0) return 0;
    return static_cast<unsigned int>((frequency.numerator + frequency.denominator / 2) /
                                     frequency.denominator);
}

unsigned int word_length(const Codec& codec) { return codec.wide() ? word_32 : word_16; }

Status link_rate(const Codec& codec, mm::audio::Rate& actual) {
    mm::mcu::Frequency running;
    const auto status = from_mcu(mm::mcu::i2s_rate(codec.wiring().i2s.instance, running));
    if (status == Status::Ok) actual = {running.numerator, running.denominator};
    return status;
}

Status link_progress(const Codec& codec, mm::mcu::I2sDirection direction,
                     mm::mcu::Progress& progress) {
    return from_mcu(mm::mcu::i2s_progress(codec.wiring().i2s.instance, direction, progress));
}

}  // namespace

// ---- Codec ------------------------------------------------------------------

Status Codec::write_register(unsigned int register_address, unsigned int value) {
    const std::array data{static_cast<std::byte>(register_address & 0xff),
                          static_cast<std::byte>(value & 0xff)};
    return from_mcu(mm::mcu::i2c_write(wiring_.i2c.instance, wiring_.address, data));
}

Status Codec::read_register(unsigned int register_address, unsigned int& value) {
    const std::array command{static_cast<std::byte>(register_address & 0xff)};
    std::array<std::byte, 1> reply{};
    const auto status = from_mcu(
        mm::mcu::i2c_write_read(wiring_.i2c.instance, wiring_.address, command, reply));
    if (status == Status::Ok) value = static_cast<unsigned int>(reply[0]);
    return status;
}

Status Codec::write_all(const unsigned char (*writes)[2], std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const auto status = write_register(writes[i][0], writes[i][1]);
        if (status != Status::Ok) return status;
    }
    return Status::Ok;
}

Status Codec::initialize() {
    if (initialized_) return Status::Ok;
    if ((!wiring_.i2s.transmit_gpio && !wiring_.i2s.receive_gpio) ||
        (wiring_.i2s.slot_bits != 16 && wiring_.i2s.slot_bits != 32) ||
        wiring_.microphone_gain > most_gain)
        return Status::BadArgument;

    auto status = from_mcu(mm::mcu::i2c_configure(wiring_.i2c));
    if (status != Status::Ok) return status;

    unsigned int first = 0;
    unsigned int second = 0;
    status = read_register(chip_id_1, first);
    if (status == Status::Ok) status = read_register(chip_id_2, second);
    if (status != Status::Ok) return status;
    if (first != expected_id_1 || second != expected_id_2) return Status::Unsupported;

    status = write_register(reset_register, reset_enter);
    if (status == Status::Ok) status = from_mcu(mm::mcu::delay_ms(wiring_.reset_delay_ms));
    if (status == Status::Ok) status = write_register(reset_register, reset_leave);
    if (status == Status::Ok) status = write_all(shared_power_up, std::size(shared_power_up));
    if (status != Status::Ok) return status;

    initialized_ = true;
    return Status::Ok;
}

Status Codec::configure(unsigned int rate_hz, mm::audio::Format& actual) {
    if (!initialized_) return Status::NotInitialized;
    if (rate_hz < slowest_hz || rate_hz > fastest_hz) return Status::BadArgument;
    if (configured_ && requested_hz_ == rate_hz) {
        actual = format_;
        return Status::Ok;
    }
    if (running_ > 0) return Status::Busy;

    // A link already running at another rate is released and configured
    // afresh.
    auto link = wiring_.i2s;
    link.rate_hz = rate_hz;
    if (configured_) {
        const auto released = from_mcu(mm::mcu::i2s_release(link.instance));
        if (released != Status::Ok) return released;
        configured_ = false;
    }
    auto status = from_mcu(mm::mcu::i2s_configure(link));
    if (status != Status::Ok) return status;

    const unsigned char clocks[][2] = {
        {clock_multiplier, static_cast<unsigned char>(wide() ? multiply_by_4 : multiply_by_8)},
        {clock_adc_osr, single_speed_osr},
        {clock_dac_osr, single_speed_osr},
        {clock_dividers, dividers_one},
    };
    status = write_all(clocks, std::size(clocks));
    if (status != Status::Ok) return status;

    mm::mcu::Frequency running;
    status = from_mcu(mm::mcu::i2s_rate(link.instance, running));
    if (status != Status::Ok) return status;

    requested_hz_ = rate_hz;
    format_ = {.rate_hz = nearest_hz(running)};
    configured_ = true;
    actual = format_;
    return Status::Ok;
}

Status Codec::rate(mm::audio::Rate& actual) const {
    if (!configured_) return Status::NotInitialized;
    return link_rate(*this, actual);
}

// ---- Output -----------------------------------------------------------------

mm::audio::Description Output::description() const {
    // The depth is the driver's own scratch: the platform's I2S buffer is
    // deeper still but mm.mcu does not say by how much, and servicing within
    // the shorter time is always enough.
    return {"es8311", slowest_hz, fastest_hz, scratch_frames};
}

Status Output::mute(bool muted) {
    return codec_.write_register(serial_in, word_length(codec_) | (muted ? serial_mute : 0));
}

Status Output::initialize() {
    if (state_ == State::Started) return Status::Busy;
    if (!codec_.wiring().i2s.transmit_gpio) return Status::BadArgument;
    auto status = codec_.initialize();
    if (status == Status::Ok) status = codec_.write_all(dac_power_up, std::size(dac_power_up));
    // The DAC stays muted until start, so the clocks starting at configure
    // are not heard.
    if (status == Status::Ok) status = mute(true);
    if (status != Status::Ok) return status;
    if (state_ == State::Idle) state_ = State::Initialized;
    return Status::Ok;
}

Status Output::configure(const mm::audio::Format& requested, mm::audio::Format& actual) {
    if (state_ == State::Idle) return Status::NotInitialized;
    if (state_ == State::Started) return Status::Busy;
    mm::audio::Format running;
    const auto status = codec_.configure(requested.rate_hz, running);
    if (status != Status::Ok) return status;
    format_ = running;
    state_ = State::Configured;
    actual = running;
    return Status::Ok;
}

Status Output::rate(mm::audio::Rate& actual) const {
    if (state_ != State::Configured && state_ != State::Started) return Status::NotInitialized;
    return codec_.rate(actual);
}

Status Output::start(mm::audio::Stream& stream) {
    if (state_ == State::Started) return Status::Busy;
    if (state_ != State::Configured || !codec_.configured() || codec_.format() != format_)
        return Status::NotInitialized;
    if (stream.format() != format_) return Status::BadArgument;
    auto status = stream.claim(mm::audio::End::Consumer);
    if (status != Status::Ok) return status;

    status = from_mcu(
        mm::mcu::i2s_start(codec_.wiring().i2s.instance, mm::mcu::I2sDirection::Transmit));
    if (status == Status::Ok) status = mute(false);
    if (status != Status::Ok) {
        stream.release(mm::audio::End::Consumer);
        return status;
    }
    stream_ = &stream;
    pending_first_ = 0;
    pending_count_ = 0;
    reported_missed_ = 0;
    codec_.started(true);
    state_ = State::Started;
    return Status::Ok;
}

// The pending run goes first, and whatever the transmitter did not accept
// stays pending for the next call.
Status Output::offer_pending() {
    std::size_t accepted = 0;
    const auto words = 2 * pending_first_;
    const auto count = 2 * pending_count_;
    const auto instance = codec_.wiring().i2s.instance;
    const auto status =
        codec_.wide()
            ? from_mcu(mm::mcu::i2s_write(
                  instance, std::span<const std::int32_t>{wide_}.subspan(words, count), accepted))
            : from_mcu(mm::mcu::i2s_write(
                  instance, std::span<const std::int16_t>{narrow_}.subspan(words, count),
                  accepted));
    if (status != Status::Ok) return status;
    pending_first_ += accepted;
    pending_count_ -= accepted;
    if (pending_count_ == 0) pending_first_ = 0;
    return Status::Ok;
}

// Read from the Stream only into an empty run, and duplicate each sample into
// both slots.
Status Output::fill_pending() {
    std::int16_t samples[scratch_frames];
    std::size_t count = 0;
    const auto status = stream_->read(samples, count);
    if (status != Status::Ok) return status;
    const auto bits = codec_.wiring().i2s.slot_bits;
    for (std::size_t i = 0; i < count; ++i) {
        if (codec_.wide()) {
            const auto word = mm::audio::from_sample(samples[i], bits);
            wide_[2 * i] = word;
            wide_[2 * i + 1] = word;
        } else {
            narrow_[2 * i] = samples[i];
            narrow_[2 * i + 1] = samples[i];
        }
    }
    pending_first_ = 0;
    pending_count_ = count;
    return Status::Ok;
}

Status Output::report_underrun() {
    mm::mcu::Progress progress;
    const auto status = link_progress(codec_, mm::mcu::I2sDirection::Transmit, progress);
    if (status != Status::Ok) return status;
    if (progress.missed != reported_missed_) {
        stream_->underran(progress.missed - reported_missed_);
        reported_missed_ = progress.missed;
    }
    return Status::Ok;
}

Status Output::service() {
    if (state_ != State::Started) return Status::NotInitialized;
    for (;;) {
        if (pending_count_ == 0) {
            const auto status = fill_pending();
            if (status != Status::Ok) return status;
            if (pending_count_ == 0) break;
        }
        const auto status = offer_pending();
        if (status != Status::Ok) return status;
        if (pending_count_ != 0) break;
    }
    return report_underrun();
}

Status Output::position(std::uint64_t& samples) const {
    if (state_ != State::Started) return Status::NotInitialized;
    mm::mcu::Progress progress;
    const auto status = link_progress(codec_, mm::mcu::I2sDirection::Transmit, progress);
    if (status == Status::Ok) samples = progress.completed;
    return status;
}

Status Output::pending(std::size_t& samples) const {
    if (state_ != State::Started) return Status::NotInitialized;
    mm::mcu::Progress progress;
    const auto status = link_progress(codec_, mm::mcu::I2sDirection::Transmit, progress);
    if (status == Status::Ok) samples = pending_count_ + progress.queued;
    return status;
}

// Muted before the transmitter's buffer is discarded, so what was cut off is
// not heard as a step; the clocks keep running.
Status Output::stop() {
    if (state_ != State::Started) return Status::NotInitialized;
    auto status = mute(true);
    if (status == Status::Ok)
        status = from_mcu(
            mm::mcu::i2s_stop(codec_.wiring().i2s.instance, mm::mcu::I2sDirection::Transmit));
    if (status != Status::Ok) return status;
    stream_->release(mm::audio::End::Consumer);
    stream_ = nullptr;
    pending_first_ = 0;
    pending_count_ = 0;
    codec_.started(false);
    state_ = State::Configured;
    return Status::Ok;
}

// Muted and idle: a started DAC is stopped first, and a later start unmutes it.
Status Output::sleep() {
    if (state_ == State::Idle) return Status::NotInitialized;
    if (state_ == State::Started) return stop();
    return mute(true);
}

// ---- Input ------------------------------------------------------------------

mm::audio::Description Input::description() const {
    return {"es8311", slowest_hz, fastest_hz, scratch_frames};
}

Status Input::mute(bool muted) {
    return codec_.write_register(serial_out, word_length(codec_) | (muted ? serial_mute : 0));
}

Status Input::initialize() {
    if (state_ == State::Started) return Status::Busy;
    if (!codec_.wiring().i2s.receive_gpio) return Status::BadArgument;
    auto status = codec_.initialize();
    if (status == Status::Ok)
        status = codec_.write_register(microphone_select,
                                       microphone_input | codec_.wiring().microphone_gain);
    if (status == Status::Ok) status = codec_.write_all(adc_power_up, std::size(adc_power_up));
    // The ADC's port stays muted until start.
    if (status == Status::Ok) status = mute(true);
    if (status != Status::Ok) return status;
    if (state_ == State::Idle) state_ = State::Initialized;
    return Status::Ok;
}

Status Input::configure(const mm::audio::Format& requested, mm::audio::Format& actual) {
    if (state_ == State::Idle) return Status::NotInitialized;
    if (state_ == State::Started) return Status::Busy;
    mm::audio::Format running;
    const auto status = codec_.configure(requested.rate_hz, running);
    if (status != Status::Ok) return status;
    format_ = running;
    state_ = State::Configured;
    actual = running;
    return Status::Ok;
}

Status Input::rate(mm::audio::Rate& actual) const {
    if (state_ != State::Configured && state_ != State::Started) return Status::NotInitialized;
    return codec_.rate(actual);
}

// The receiver discards what it held when it starts, so nothing converted
// before start reaches the Stream.
Status Input::start(mm::audio::Stream& stream) {
    if (state_ == State::Started) return Status::Busy;
    if (state_ != State::Configured || !codec_.configured() || codec_.format() != format_)
        return Status::NotInitialized;
    if (stream.format() != format_) return Status::BadArgument;
    auto status = stream.claim(mm::audio::End::Producer);
    if (status != Status::Ok) return status;

    status = from_mcu(
        mm::mcu::i2s_start(codec_.wiring().i2s.instance, mm::mcu::I2sDirection::Receive));
    if (status == Status::Ok) status = mute(false);
    if (status != Status::Ok) {
        stream.release(mm::audio::End::Producer);
        return status;
    }
    stream_ = &stream;
    reported_missed_ = 0;
    codec_.started(true);
    state_ = State::Started;
    return Status::Ok;
}

// Up to frames from the receiver, converted to samples from input_slot.
Status Input::take(std::size_t frames, std::size_t& count) {
    const auto instance = codec_.wiring().i2s.instance;
    const auto status =
        codec_.wide()
            ? from_mcu(mm::mcu::i2s_read(
                  instance, std::span<std::int32_t>{wide_}.first(2 * frames), count))
            : from_mcu(mm::mcu::i2s_read(
                  instance, std::span<std::int16_t>{narrow_}.first(2 * frames), count));
    if (status != Status::Ok) return status;
    const std::size_t slot = codec_.wiring().input_slot == Slot::Left ? 0 : 1;
    const auto bits = codec_.wiring().i2s.slot_bits;
    for (std::size_t i = 0; i < count; ++i)
        samples_[i] = codec_.wide() ? mm::audio::to_sample(wide_[2 * i + slot], bits)
                                    : narrow_[2 * i + slot];
    return Status::Ok;
}

Status Input::report_overrun() {
    mm::mcu::Progress progress;
    const auto status = link_progress(codec_, mm::mcu::I2sDirection::Receive, progress);
    if (status != Status::Ok) return status;
    if (progress.missed != reported_missed_) {
        stream_->overran(progress.missed - reported_missed_);
        reported_missed_ = progress.missed;
    }
    return Status::Ok;
}

// Never more than the Stream has room for, so what is taken is written whole:
// the Stream has one producer, and its room only grows between the two calls.
Status Input::service() {
    if (state_ != State::Started) return Status::NotInitialized;
    for (;;) {
        const auto room = std::min(stream_->writable(), scratch_frames);
        if (room == 0) break;
        std::size_t count = 0;
        auto status = take(room, count);
        if (status != Status::Ok) return status;
        if (count == 0) break;
        std::size_t written = 0;
        status = stream_->write(std::span<const std::int16_t>{samples_}.first(count), written);
        if (status != Status::Ok) return status;
        if (written < count) stream_->overran(count - written);
    }
    return report_overrun();
}

Status Input::position(std::uint64_t& samples) const {
    if (state_ != State::Started) return Status::NotInitialized;
    mm::mcu::Progress progress;
    const auto status = link_progress(codec_, mm::mcu::I2sDirection::Receive, progress);
    if (status == Status::Ok) samples = progress.completed;
    return status;
}

Status Input::stop() {
    if (state_ != State::Started) return Status::NotInitialized;
    auto status = mute(true);
    if (status == Status::Ok)
        status = from_mcu(
            mm::mcu::i2s_stop(codec_.wiring().i2s.instance, mm::mcu::I2sDirection::Receive));
    if (status != Status::Ok) return status;
    stream_->release(mm::audio::End::Producer);
    stream_ = nullptr;
    codec_.started(false);
    state_ = State::Configured;
    return Status::Ok;
}

Status Input::sleep() {
    if (state_ == State::Idle) return Status::NotInitialized;
    if (state_ == State::Started) return stop();
    return mute(true);
}

}  // namespace mm::audio::es8311
