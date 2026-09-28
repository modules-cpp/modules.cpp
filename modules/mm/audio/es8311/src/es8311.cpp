// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

module mm.audio.es8311;

namespace mm::audio::es8311 {

namespace {

// Eight-bit register addresses, one byte on the wire, per the ASoC driver.
constexpr unsigned int reset_register = 0x00;
constexpr unsigned int sys1_register = 0x0b;
constexpr unsigned int sys2_register = 0x0c;
constexpr unsigned int sdp_out_register = 0x0a;

// The reset the driver performs: enter reset mode, pause, then leave it.
constexpr std::byte reset_enter = std::byte{0x1f};   // CSM off, reset bits set
constexpr std::byte reset_leave = std::byte{0x80};   // CSM on, reset bits clear

// Power-up: the driver clears the two system registers.
constexpr std::byte power_up = std::byte{0x00};

// The output channel, for I2S playback: I2S format (bits one and two zero),
// sixteen-bit word length (bits four to two), and the mute bit clear.
constexpr std::byte sdp_out_playback = std::byte{0x0c};

// The same output register with its mute bit set, for shutdown.
constexpr std::byte sdp_out_mute = std::byte{0x4c};

// One sixteen-bit sample is two bytes on the I2S link, little-endian.
constexpr std::size_t frame_bytes = 2;

[[nodiscard]] unsigned int byte_value(std::byte value) {
    return static_cast<unsigned int>(value);
}

}  // namespace

mm::audio::Status Codec::from_mcu(mm::mcu::Status status) const {
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

mm::audio::Description Codec::description() const {
    return {"es8311", wiring_.rate, 1, mm::audio::Direction::Output};
}

mm::audio::Status Codec::write_register(unsigned int register_address, unsigned int value) {
    const std::array data{static_cast<std::byte>(register_address),
                           static_cast<std::byte>(value)};
    return from_mcu(mm::mcu::i2c_write(wiring_.i2c.instance, wiring_.address, data));
}

mm::audio::Status Codec::initialize() {
    if (wiring_.rate == 0) return mm::audio::Status::BadArgument;

    auto status = from_mcu(mm::mcu::i2c_configure(wiring_.i2c));
    if (status != mm::audio::Status::Ok) return status;

    // Reset, the way the ASoC driver does it: enter reset mode, pause, leave it.
    status = write_register(reset_register, byte_value(reset_enter));
    if (status != mm::audio::Status::Ok) return status;
    status = from_mcu(mm::mcu::delay_ms(wiring_.reset_delay_ms));
    if (status != mm::audio::Status::Ok) return status;
    status = write_register(reset_register, byte_value(reset_leave));
    if (status != mm::audio::Status::Ok) return status;

    // Power up the state machine by clearing the two system registers.
    status = write_register(sys1_register, byte_value(power_up));
    if (status != mm::audio::Status::Ok) return status;
    status = write_register(sys2_register, byte_value(power_up));
    if (status != mm::audio::Status::Ok) return status;

    // Program the output channel for I2S playback.
    status = write_register(sdp_out_register, byte_value(sdp_out_playback));
    if (status != mm::audio::Status::Ok) return status;

    // The I2S link carries the samples.
    status = from_mcu(mm::mcu::i2s_configure(wiring_.i2s));
    if (status != mm::audio::Status::Ok) return status;

    ready_ = true;
    return mm::audio::Status::Ok;
}

mm::audio::Status Codec::play(std::span<const mm::audio::Sample> samples) {
    if (!ready_) return mm::audio::Status::NotInitialized;
    if (samples.empty()) return mm::audio::Status::BadArgument;

    // One sixteen-bit sample is two bytes on the link, little-endian.
    std::vector<std::byte> wire(samples.size() * frame_bytes);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto value = static_cast<std::uint16_t>(samples[i]);  // the 16-bit pattern
        wire[i * frame_bytes] = static_cast<std::byte>(value & 0xff);
        wire[i * frame_bytes + 1] = static_cast<std::byte>(value >> 8);
    }
    return from_mcu(mm::mcu::i2s_write(wiring_.i2s.instance, wire));
}

mm::audio::Status Codec::shutdown() {
    if (!ready_) return mm::audio::Status::NotInitialized;
    auto status = write_register(sdp_out_register, byte_value(sdp_out_mute));
    if (status != mm::audio::Status::Ok) return status;
    ready_ = false;
    return mm::audio::Status::Ok;
}

}  // namespace mm::audio::es8311
