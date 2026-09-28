// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.audio.microphone;

import mm.audio;
import mm.mcu;

export namespace mm::audio::microphone {

// Everything the microphone needs to know about the board it is soldered to.
// A provider fills this in; the driver reads it and never guesses.
struct Wiring {
    unsigned int channel = 0;   // the ADC channel the microphone's output is on
    unsigned int rate = 0;      // the sample rate the caller captures at
    unsigned int bits = 12;     // the ADC's width, for the count-to-sample scale
};

class Microphone : public mm::audio::Microphone {
public:
    Microphone(const Wiring& wiring) : wiring_(wiring) {}

    [[nodiscard]] mm::audio::Description description() const override;
    [[nodiscard]] mm::audio::Status initialize() override;
    [[nodiscard]] mm::audio::Status capture(std::span<mm::audio::Sample>,
                                            std::size_t&) override;
    [[nodiscard]] mm::audio::Status shutdown() override;

private:
    [[nodiscard]] mm::audio::Status from_mcu(mm::mcu::Status status) const;

    Wiring wiring_;
    bool ready_ = false;
};

}
