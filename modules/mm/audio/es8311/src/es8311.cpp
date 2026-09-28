// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Register values follow the ES8311 datasheet's map as Espressif's own driver
// programs it for a slave-mode DAC with the master clock taken from the bit
// clock. They are unqualified on hardware until a board with this codec plays
// through them.
module;

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
constexpr unsigned int system_power_1 = 0x0b;
constexpr unsigned int system_power_2 = 0x0c;
constexpr unsigned int analog_power = 0x0d;
constexpr unsigned int analog_bias = 0x0e;
constexpr unsigned int system_reference_1 = 0x10;
constexpr unsigned int system_reference_2 = 0x11;
constexpr unsigned int dac_power = 0x12;
constexpr unsigned int output_driver = 0x13;
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

// SDP IN: I2S framing (bits one and zero clear), the word length in bits four
// to two -- three for sixteen bits, four for thirty-two -- and the mute bit.
constexpr unsigned int word_16 = 0x0c;
constexpr unsigned int word_32 = 0x10;
constexpr unsigned int serial_mute = 0x40;

constexpr unsigned int zero_decibels = 0xbf;

// The power-up after reset, in order: the system and reference registers
// cleared or set as the vendor sequence does, the analog section, the DAC,
// the output driver, the DAC ramp with its equaliser bypassed, and unity
// volume.
constexpr unsigned char power_up[][2] = {
    {clock_manager, clocks_from_bit_clock},
    {system_power_1, 0x00},
    {system_power_2, 0x00},
    {system_reference_1, 0x1f},
    {system_reference_2, 0x7f},
    {analog_power, 0x01},
    {analog_bias, 0x02},
    {dac_power, 0x00},
    {output_driver, 0x10},
    {dac_ramp, 0x08},
    {dac_volume, zero_decibels},
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

}  // namespace

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

mm::audio::Description Codec::description() const {
    // The depth is the driver's own scratch: the platform's I2S buffer is
    // deeper still but mm.mcu does not say by how much, and servicing within
    // the shorter time is always enough.
    return {"es8311", slowest_hz, fastest_hz, scratch_frames};
}

Status Codec::initialize() {
    if (state_ == State::Started) return Status::Busy;
    if (!wiring_.i2s.transmit_gpio ||
        (wiring_.i2s.slot_bits != 16 && wiring_.i2s.slot_bits != 32))
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
    if (status == Status::Ok) status = write_all(power_up, std::size(power_up));
    // The DAC stays muted until start, so the clocks starting at configure
    // are not heard.
    if (status == Status::Ok)
        status = write_register(serial_in, (wide() ? word_32 : word_16) | serial_mute);
    if (status != Status::Ok) return status;

    state_ = State::Initialized;
    return Status::Ok;
}

Status Codec::configure(const mm::audio::Format& requested, mm::audio::Format& actual) {
    if (state_ == State::Idle) return Status::NotInitialized;
    if (state_ == State::Started) return Status::Busy;
    if (requested.rate_hz < slowest_hz || requested.rate_hz > fastest_hz)
        return Status::BadArgument;

    // A link already running at another rate is released and configured
    // afresh; the same rate is idempotent in mm.mcu.
    auto link = wiring_.i2s;
    link.rate_hz = requested.rate_hz;
    if (state_ == State::Configured && format_.rate_hz != requested.rate_hz) {
        const auto released = from_mcu(mm::mcu::i2s_release(link.instance));
        if (released != Status::Ok) return released;
        state_ = State::Initialized;
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

    format_ = {.rate_hz = nearest_hz(running)};
    state_ = State::Configured;
    actual = format_;
    return Status::Ok;
}

Status Codec::rate(mm::audio::Rate& actual) const {
    if (state_ != State::Configured && state_ != State::Started) return Status::NotInitialized;
    mm::mcu::Frequency running;
    const auto status = from_mcu(mm::mcu::i2s_rate(wiring_.i2s.instance, running));
    if (status == Status::Ok) actual = {running.numerator, running.denominator};
    return status;
}

Status Codec::start(mm::audio::Stream& stream) {
    if (state_ == State::Started) return Status::Busy;
    if (state_ != State::Configured) return Status::NotInitialized;
    if (stream.format() != format_) return Status::BadArgument;
    auto status = stream.claim(mm::audio::End::Consumer);
    if (status != Status::Ok) return status;

    status = from_mcu(mm::mcu::i2s_start(wiring_.i2s.instance, mm::mcu::I2sDirection::Transmit));
    if (status == Status::Ok)
        status = write_register(serial_in, wide() ? word_32 : word_16);
    if (status != Status::Ok) {
        stream.release(mm::audio::End::Consumer);
        return status;
    }
    stream_ = &stream;
    pending_first_ = 0;
    pending_count_ = 0;
    reported_missed_ = 0;
    state_ = State::Started;
    return Status::Ok;
}

// The pending run goes first, and whatever the transmitter did not accept
// stays pending for the next call.
Status Codec::offer_pending() {
    std::size_t accepted = 0;
    const auto words = 2 * pending_first_;
    const auto count = 2 * pending_count_;
    const auto status =
        wide() ? from_mcu(mm::mcu::i2s_write(
                     wiring_.i2s.instance,
                     std::span<const std::int32_t>{wide_}.subspan(words, count), accepted))
               : from_mcu(mm::mcu::i2s_write(
                     wiring_.i2s.instance,
                     std::span<const std::int16_t>{narrow_}.subspan(words, count), accepted));
    if (status != Status::Ok) return status;
    pending_first_ += accepted;
    pending_count_ -= accepted;
    if (pending_count_ == 0) pending_first_ = 0;
    return Status::Ok;
}

// Read from the Stream only into an empty run, and duplicate each sample into
// both slots.
Status Codec::fill_pending() {
    std::int16_t samples[scratch_frames];
    std::size_t count = 0;
    const auto status = stream_->read(samples, count);
    if (status != Status::Ok) return status;
    for (std::size_t i = 0; i < count; ++i) {
        if (wide()) {
            const auto word = mm::audio::from_sample(samples[i], wiring_.i2s.slot_bits);
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

Status Codec::report_underrun() {
    mm::mcu::Progress progress;
    const auto status = from_mcu(mm::mcu::i2s_progress(
        wiring_.i2s.instance, mm::mcu::I2sDirection::Transmit, progress));
    if (status != Status::Ok) return status;
    if (progress.missed != reported_missed_) {
        stream_->underran(progress.missed - reported_missed_);
        reported_missed_ = progress.missed;
    }
    return Status::Ok;
}

Status Codec::service() {
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

Status Codec::position(std::uint64_t& samples) const {
    if (state_ != State::Started) return Status::NotInitialized;
    mm::mcu::Progress progress;
    const auto status = from_mcu(mm::mcu::i2s_progress(
        wiring_.i2s.instance, mm::mcu::I2sDirection::Transmit, progress));
    if (status == Status::Ok) samples = progress.completed;
    return status;
}

Status Codec::pending(std::size_t& samples) const {
    if (state_ != State::Started) return Status::NotInitialized;
    mm::mcu::Progress progress;
    const auto status = from_mcu(mm::mcu::i2s_progress(
        wiring_.i2s.instance, mm::mcu::I2sDirection::Transmit, progress));
    if (status == Status::Ok) samples = pending_count_ + progress.queued;
    return status;
}

// Muted before the transmitter's buffer is discarded, so what was cut off
// is not heard as a step; the clocks keep running.
Status Codec::stop() {
    if (state_ != State::Started) return Status::NotInitialized;
    auto status = write_register(serial_in, (wide() ? word_32 : word_16) | serial_mute);
    if (status == Status::Ok)
        status = from_mcu(
            mm::mcu::i2s_stop(wiring_.i2s.instance, mm::mcu::I2sDirection::Transmit));
    if (status != Status::Ok) return status;
    stream_->release(mm::audio::End::Consumer);
    stream_ = nullptr;
    pending_first_ = 0;
    pending_count_ = 0;
    state_ = State::Configured;
    return Status::Ok;
}

// Muted and idle: a started codec is stopped first, and a later start
// unmutes it.
Status Codec::sleep() {
    if (state_ == State::Idle) return Status::NotInitialized;
    if (state_ == State::Started) return stop();
    return write_register(serial_in, (wide() ? word_32 : word_16) | serial_mute);
}

}  // namespace mm::audio::es8311
