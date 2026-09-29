// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>

export module mm.audio.es8311;

import mm.audio;
import mm.mcu;

export namespace mm::audio::es8311 {

enum class Slot { Left, Right };

// Everything the codec needs to know about the board it is soldered to. A
// board's provider fills this in; the driver reads it and never guesses.
//
// The codec is controlled over I2C at 0x18, or 0x19 with its CE pin high, and
// runs over I2S as a slave: the platform drives both clocks, and the codec
// derives its internal master clock from the bit clock, so no MCLK pin is
// needed. i2s names the link's pins and slot width -- sixteen or thirty-two
// bits -- with a transmit line for the DAC, a receive line for the ADC, or
// both; its rate is ignored, because configure sets it.
//
// The ADC is mono and its samples are read from input_slot. microphone_gain
// is the analog PGA's step, zero to ten, three decibels a step; ten, thirty
// decibels, is what the vendor sequence selects for an electret capsule.
struct Wiring {
    mm::mcu::I2cConfiguration i2c;
    unsigned int address = 0x18;
    mm::mcu::I2sConfiguration i2s;
    unsigned long reset_delay_ms = 5;
    Slot input_slot = Slot::Left;
    unsigned int microphone_gain = 10;
};

// The chip: the control bus, the reset, the clocks, and the one I2S link the
// DAC and the ADC share. It is not an audio device itself; an Output and an
// Input are, and both hold one Codec. A board whose ES8311 plays and records
// constructs one Codec and one of each, and registers them as its Speaker and
// its Microphone.
//
// The link has one word clock, so the two directions run at one rate. The
// first configure sets it; a configure at another rate reconfigures the link
// only while neither direction is started, and is Busy otherwise. A direction
// configured at a rate the link has since left must be configured again.
class Codec {
public:
    explicit Codec(const Wiring& wiring) : wiring_(wiring) {}
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;
    Codec(Codec&&) = delete;
    Codec& operator=(Codec&&) = delete;
    ~Codec() = default;

    // Identifies, resets, and powers up what both directions share. Only the
    // first successful call touches the chip; later ones answer Ok, so one
    // direction's initialize never resets the codec under the other.
    [[nodiscard]] mm::audio::Status initialize();

    // Starts the link at the nearest rate it holds and programs the clock
    // multiplier. actual is written only on Ok.
    [[nodiscard]] mm::audio::Status configure(unsigned int rate_hz,
                                              mm::audio::Format& actual);
    [[nodiscard]] mm::audio::Status rate(mm::audio::Rate& actual) const;

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] bool configured() const { return configured_; }
    [[nodiscard]] mm::audio::Format format() const { return format_; }
    [[nodiscard]] const Wiring& wiring() const { return wiring_; }
    [[nodiscard]] bool wide() const { return wiring_.i2s.slot_bits != 16; }

    [[nodiscard]] mm::audio::Status write_register(unsigned int register_address,
                                                   unsigned int value);
    [[nodiscard]] mm::audio::Status write_all(const unsigned char (*writes)[2],
                                              std::size_t count);

    // How many directions are started; the link's rate is fixed while any is.
    void started(bool running) { running_ += running ? 1 : -1; }

private:
    [[nodiscard]] mm::audio::Status read_register(unsigned int register_address,
                                                  unsigned int& value);

    Wiring wiring_;
    bool initialized_ = false;
    bool configured_ = false;
    int running_ = 0;
    unsigned int requested_hz_ = 0;
    mm::audio::Format format_;
};

// The ES8311's DAC as an mm.audio Out. It keeps the Out contract: service
// offers the pending run first and reads from the Stream only when nothing is
// pending, so a sample read is played or discarded by stop and never lost;
// the I2S transmitter's silence on underrun is reported to the Stream;
// position and pending come from the transmitter's progress. A mono sample
// goes to both slots, so the DAC plays it whichever slot it takes.
class Output final : public mm::audio::Out {
public:
    explicit Output(Codec& codec) : codec_(codec) {}
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;
    ~Output() override = default;

    [[nodiscard]] mm::audio::Description description() const override;
    [[nodiscard]] mm::audio::Status initialize() override;
    [[nodiscard]] mm::audio::Status configure(const mm::audio::Format& requested,
                                              mm::audio::Format& actual) override;
    [[nodiscard]] mm::audio::Status rate(mm::audio::Rate& actual) const override;
    [[nodiscard]] mm::audio::Status start(mm::audio::Stream& stream) override;
    [[nodiscard]] mm::audio::Status service() override;
    [[nodiscard]] mm::audio::Status position(std::uint64_t& samples) const override;
    [[nodiscard]] mm::audio::Status pending(std::size_t& samples) const override;
    [[nodiscard]] mm::audio::Status stop() override;
    [[nodiscard]] mm::audio::Status sleep() override;

    // The frames service moves in one pass, and the most it holds pending.
    static constexpr std::size_t scratch_frames = 64;

private:
    enum class State { Idle, Initialized, Configured, Started };

    [[nodiscard]] mm::audio::Status offer_pending();
    [[nodiscard]] mm::audio::Status fill_pending();
    [[nodiscard]] mm::audio::Status report_underrun();
    [[nodiscard]] mm::audio::Status mute(bool muted);

    Codec& codec_;
    State state_ = State::Idle;
    mm::audio::Format format_;
    mm::audio::Stream* stream_ = nullptr;
    std::uint32_t reported_missed_ = 0;

    // The pending run: frames read from the Stream and not yet accepted by
    // the transmitter, as words in the configured width.
    std::int16_t narrow_[2 * scratch_frames] = {};
    std::int32_t wide_[2 * scratch_frames] = {};
    std::size_t pending_first_ = 0;
    std::size_t pending_count_ = 0;
};

// The ES8311's ADC as an mm.audio In, recording its analog microphone input.
// It keeps the In contract: service takes from the receiver no more than the
// Stream has room for, so a sample taken is never dropped here; frames the
// receiver dropped because the application fell behind are reported to the
// Stream as overrun; position is the receiver's progress. Each frame's
// input_slot word is the sample.
class Input final : public mm::audio::In {
public:
    explicit Input(Codec& codec) : codec_(codec) {}
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;
    ~Input() override = default;

    [[nodiscard]] mm::audio::Description description() const override;
    [[nodiscard]] mm::audio::Status initialize() override;
    [[nodiscard]] mm::audio::Status configure(const mm::audio::Format& requested,
                                              mm::audio::Format& actual) override;
    [[nodiscard]] mm::audio::Status rate(mm::audio::Rate& actual) const override;
    [[nodiscard]] mm::audio::Status start(mm::audio::Stream& stream) override;
    [[nodiscard]] mm::audio::Status service() override;
    [[nodiscard]] mm::audio::Status position(std::uint64_t& samples) const override;
    [[nodiscard]] mm::audio::Status stop() override;
    [[nodiscard]] mm::audio::Status sleep() override;

    // The frames service takes in one pass.
    static constexpr std::size_t scratch_frames = 64;

private:
    enum class State { Idle, Initialized, Configured, Started };

    [[nodiscard]] mm::audio::Status take(std::size_t frames, std::size_t& count);
    [[nodiscard]] mm::audio::Status report_overrun();
    [[nodiscard]] mm::audio::Status mute(bool muted);

    Codec& codec_;
    State state_ = State::Idle;
    mm::audio::Format format_;
    mm::audio::Stream* stream_ = nullptr;
    std::uint32_t reported_missed_ = 0;

    std::int16_t narrow_[2 * scratch_frames] = {};
    std::int32_t wide_[2 * scratch_frames] = {};
    std::int16_t samples_[scratch_frames] = {};
};

}
