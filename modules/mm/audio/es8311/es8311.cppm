// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.audio.es8311;

import mm.audio;
import mm.mcu;

export namespace mm::audio::es8311 {

// Everything the codec needs to know about the board it is soldered to. A
// provider fills this in; the driver reads it and never guesses.
struct Wiring {
    mm::mcu::I2cConfiguration i2c;
    unsigned int address = 0x06;     // the codec's I2C address, per its strap pins
    mm::mcu::I2sConfiguration i2s;
    unsigned int rate = 0;           // the sample rate playback runs at
    unsigned long reset_delay_ms = 5; // the pause between entering and leaving reset
};

class Codec : public mm::audio::Codec {
public:
    Codec(const Wiring& wiring) : wiring_(wiring) {}

    [[nodiscard]] mm::audio::Description description() const override;
    [[nodiscard]] mm::audio::Status initialize() override;
    [[nodiscard]] mm::audio::Status play(std::span<const mm::audio::Sample>) override;
    [[nodiscard]] mm::audio::Status shutdown() override;

private:
    [[nodiscard]] mm::audio::Status write_register(unsigned int register_address,
                                                   unsigned int value);
    [[nodiscard]] mm::audio::Status from_mcu(mm::mcu::Status status) const;

    Wiring wiring_;
    bool ready_ = false;
};

}
