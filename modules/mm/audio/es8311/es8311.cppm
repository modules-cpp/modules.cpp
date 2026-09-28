// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>

export module mm.audio.es8311;

import mm.audio;
import mm.mcu;

export namespace mm::audio::es8311 {

// Everything the codec needs to know about the board it is soldered to. A
// board's provider fills this in; the driver reads it and never guesses.
//
// The codec is controlled over I2C at 0x18, or 0x19 with its CE pin high, and
// plays over I2S as a slave: the platform drives both clocks, and the codec
// derives its internal master clock from the bit clock, so no MCLK pin is
// needed. i2s names the link's pins and slot width -- sixteen or thirty-two
// bits -- and must have a transmit line; its rate is ignored, because
// configure sets it.
struct Wiring {
    mm::mcu::I2cConfiguration i2c;
    unsigned int address = 0x18;
    mm::mcu::I2sConfiguration i2s;
    unsigned long reset_delay_ms = 5;
};

// The ES8311's DAC as an mm.audio Out. It is a portable driver in the place
// mm.imu.qmi8658 holds for the IMU: a board whose speaker is an ES8311
// constructs one with its wiring and registers it as its Speaker.
//
// It keeps the Out contract: service offers the pending run first and reads
// from the Stream only when nothing is pending, so a sample read is played or
// discarded by stop and never lost; the I2S transmitter's silence on underrun
// is reported to the Stream; position and pending come from the transmitter's
// progress. A mono sample goes to both slots, so the codec's one DAC channel
// plays it whichever slot it takes.
class Codec final : public mm::audio::Out {
public:
    explicit Codec(const Wiring& wiring) : wiring_(wiring) {}
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;
    Codec(Codec&&) = delete;
    Codec& operator=(Codec&&) = delete;
    ~Codec() override = default;

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

    [[nodiscard]] mm::audio::Status write_register(unsigned int register_address,
                                                   unsigned int value);
    [[nodiscard]] mm::audio::Status read_register(unsigned int register_address,
                                                  unsigned int& value);
    [[nodiscard]] mm::audio::Status write_all(const unsigned char (*writes)[2],
                                              std::size_t count);
    [[nodiscard]] mm::audio::Status offer_pending();
    [[nodiscard]] mm::audio::Status fill_pending();
    [[nodiscard]] mm::audio::Status report_underrun();
    [[nodiscard]] bool wide() const { return wiring_.i2s.slot_bits != 16; }

    Wiring wiring_;
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

}
