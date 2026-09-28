// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <optional>

export module platform.rp2350_touch_lcd_154.audio;

import mm.audio;
import mm.audio.es8311;
import mm.mcu;

// Module linkage rather than an unnamed namespace: the two device classes have
// virtual functions, and docs/modules-c++20.mdy says why that matters.
namespace platform::rp2350_touch_lcd_154::audio_provider {

constexpr unsigned int amplifier_gpio = 0;      // NS4150B CTRL, NS_MODE
constexpr unsigned int master_clock_gpio = 3;   // the codec's MCLK input

mm::audio::es8311::Wiring board_wiring() {
    mm::audio::es8311::Wiring result;
    result.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000};
    result.address = 0x18;
    result.i2s.instance = 0;
    result.i2s.bit_clock_gpio = 4;
    result.i2s.word_clock_gpio = 5;
    result.i2s.transmit_gpio = 1;
    result.i2s.receive_gpio = 2;
    result.i2s.slot_bits = 16;
    result.reset_delay_ms = 20;
    result.input_slot = mm::audio::es8311::Slot::Left;
    result.microphone_gain = 10;
    return result;
}

mm::audio::es8311::Codec codec{board_wiring()};

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

// A pin driven as an output at a level, the one shape both pins here take.
mm::audio::Status drive(unsigned int pin, bool high) {
    auto status = from_mcu(mm::mcu::gpio_configure(pin, mm::mcu::Direction::Out,
                                                   mm::mcu::Pull::None));
    if (status == mm::audio::Status::Ok) status = from_mcu(mm::mcu::gpio_write(pin, high));
    return status;
}

// The codec's master clock comes from the bit clock, so its MCLK input is
// held low rather than left to float.
mm::audio::Status hold_master_clock() { return drive(master_clock_gpio, false); }

class Speaker final : public mm::audio::Out {
public:
    Speaker() = default;
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;
    Speaker(Speaker&&) = delete;
    Speaker& operator=(Speaker&&) = delete;
    ~Speaker() override = default;

    [[nodiscard]] mm::audio::Description description() const override {
        return output_.description();
    }

    [[nodiscard]] mm::audio::Status initialize() override {
        auto status = hold_master_clock();
        if (status == mm::audio::Status::Ok) status = drive(amplifier_gpio, false);
        if (status == mm::audio::Status::Ok) status = output_.initialize();
        return status;
    }

    [[nodiscard]] mm::audio::Status configure(const mm::audio::Format& requested,
                                              mm::audio::Format& actual) override {
        return output_.configure(requested, actual);
    }

    [[nodiscard]] mm::audio::Status rate(mm::audio::Rate& actual) const override {
        return output_.rate(actual);
    }

    // The amplifier comes on after the codec's port is unmuted.
    [[nodiscard]] mm::audio::Status start(mm::audio::Stream& stream) override {
        auto status = output_.start(stream);
        if (status != mm::audio::Status::Ok) return status;
        status = from_mcu(mm::mcu::gpio_write(amplifier_gpio, true));
        if (status != mm::audio::Status::Ok) static_cast<void>(output_.stop());
        return status;
    }

    [[nodiscard]] mm::audio::Status service() override { return output_.service(); }

    [[nodiscard]] mm::audio::Status position(std::uint64_t& samples) const override {
        return output_.position(samples);
    }

    [[nodiscard]] mm::audio::Status pending(std::size_t& samples) const override {
        return output_.pending(samples);
    }

    // And goes off before the codec mutes.
    [[nodiscard]] mm::audio::Status stop() override {
        const auto status = from_mcu(mm::mcu::gpio_write(amplifier_gpio, false));
        if (status != mm::audio::Status::Ok) return status;
        return output_.stop();
    }

    [[nodiscard]] mm::audio::Status sleep() override {
        const auto status = from_mcu(mm::mcu::gpio_write(amplifier_gpio, false));
        if (status != mm::audio::Status::Ok) return status;
        return output_.sleep();
    }

private:
    mm::audio::es8311::Output output_{codec};
};

class Microphone final : public mm::audio::In {
public:
    Microphone() = default;
    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;
    Microphone(Microphone&&) = delete;
    Microphone& operator=(Microphone&&) = delete;
    ~Microphone() override = default;

    [[nodiscard]] mm::audio::Description description() const override {
        return input_.description();
    }

    [[nodiscard]] mm::audio::Status initialize() override {
        const auto status = hold_master_clock();
        if (status != mm::audio::Status::Ok) return status;
        return input_.initialize();
    }

    [[nodiscard]] mm::audio::Status configure(const mm::audio::Format& requested,
                                              mm::audio::Format& actual) override {
        return input_.configure(requested, actual);
    }

    [[nodiscard]] mm::audio::Status rate(mm::audio::Rate& actual) const override {
        return input_.rate(actual);
    }

    [[nodiscard]] mm::audio::Status start(mm::audio::Stream& stream) override {
        return input_.start(stream);
    }

    [[nodiscard]] mm::audio::Status service() override { return input_.service(); }

    [[nodiscard]] mm::audio::Status position(std::uint64_t& samples) const override {
        return input_.position(samples);
    }

    [[nodiscard]] mm::audio::Status stop() override { return input_.stop(); }

    [[nodiscard]] mm::audio::Status sleep() override { return input_.sleep(); }

private:
    mm::audio::es8311::Input input_{codec};
};

Speaker speaker;
Microphone microphone;

struct Register {
    Register() {
        mm::audio::set_out(speaker);
        mm::audio::set_in(microphone);
    }
};

const Register registered;

}  // namespace platform::rp2350_touch_lcd_154::audio_provider
