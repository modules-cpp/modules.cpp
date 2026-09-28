// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

import mm.mcu;
import mm.audio;
import mm.audio.es8311;
import mm.test;

void mm_test_es8311_reset();
void mm_test_es8311_force(mm::mcu::Status status);
bool mm_test_es8311_i2c_configured();
bool mm_test_es8311_i2s_configured();
std::size_t mm_test_es8311_write_count();
unsigned int mm_test_es8311_write(std::size_t index);
unsigned long mm_test_es8311_ticks();
std::size_t mm_test_es8311_i2s_size();
unsigned int mm_test_es8311_i2s_byte(std::size_t index);

namespace {

using mm::test::expect;
using mm::audio::Status;

mm::audio::es8311::Wiring wiring() {
    return {.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
            .address = 0x06,
            .i2s = {.instance = 1, .data_gpio = 8, .clock_gpio = 9, .word_select_gpio = 10,
                    .baud = 12'288'000},
            .rate = 44'100,
            .reset_delay_ms = 5};
}

void initializes_through_the_reset_and_power_up() {
    mm_test_es8311_reset();
    mm::audio::es8311::Codec codec{wiring()};
    expect(codec.initialize() == Status::Ok, "a codec at its address initializes");

    // The transcript, register and value, in the order the driver performs it.
    expect(mm_test_es8311_write_count() == 5, "the driver performs five register writes");
    expect(mm_test_es8311_write(0) == (0x00 + 0x1fu * 0x100),
           "reset mode is entered on the reset register");
    expect(mm_test_es8311_write(1) == (0x00 + 0x80u * 0x100),
           "reset mode is left on the reset register");
    expect(mm_test_es8311_write(2) == (0x0b + 0x00u * 0x100), "the first system register clears");
    expect(mm_test_es8311_write(3) == (0x0c + 0x00u * 0x100), "the second system register clears");
    expect(mm_test_es8311_write(4) == (0x0a + 0x0cu * 0x100),
           "the output channel is programmed for I2S playback");

    expect(mm_test_es8311_ticks() == 5, "the reset pause is observed");
    expect(mm_test_es8311_i2c_configured() && mm_test_es8311_i2s_configured(),
           "both the control bus and the data link are configured");

    const auto description = codec.description();
    expect(description.name == "es8311" && description.rate == 44'100 &&
               description.channels == 1 &&
               description.direction == mm::audio::Direction::Output,
           "the description reports the output the provider described");
}

// One sixteen-bit sample is two bytes on the I2S link, little-endian, and a
// negative sample carries its sign in the bytes rather than losing it.
void play_ships_sixteen_bit_little_endian_frames() {
    mm_test_es8311_reset();
    mm::audio::es8311::Codec codec{wiring()};
    expect(codec.initialize() == Status::Ok, "the codec initializes");

    std::array<mm::audio::Sample, 2> samples{0x1234, -2};
    expect(codec.play(samples) == Status::Ok, "a playback succeeds once initialized");
    expect(mm_test_es8311_i2s_size() == 4, "two samples ship four bytes down the link");
    expect(mm_test_es8311_i2s_byte(0) == 0x34 && mm_test_es8311_i2s_byte(1) == 0x12,
           "the first sample is little-endian");
    expect(mm_test_es8311_i2s_byte(2) == 0xfe && mm_test_es8311_i2s_byte(3) == 0xff,
           "a negative sample keeps its sign in the bytes");
}

void play_before_initialize_reports_not_initialized() {
    mm_test_es8311_reset();
    mm::audio::es8311::Codec codec{wiring()};
    std::array<mm::audio::Sample, 2> samples{0, 0};
    expect(codec.play(samples) == Status::NotInitialized, "no playback before the handshake");
}

void rejects_invalid_wiring() {
    mm_test_es8311_reset();
    const mm::audio::es8311::Wiring wiring_no_rate{.i2c = {1, 6, 7, 400'000},
                                                   .address = 0x06,
                                                   .i2s = {1, 8, 9, 10, 12'288'000},
                                                   .rate = 0,
                                                   .reset_delay_ms = 5};
    mm::audio::es8311::Codec no_rate{wiring_no_rate};
    expect(no_rate.initialize() == Status::BadArgument,
           "a codec without a sample rate is refused");
    expect(mm_test_es8311_i2c_configured() == false, "the bus is not configured in the refusal");
}

void rejects_an_empty_playback() {
    mm_test_es8311_reset();
    mm::audio::es8311::Codec codec{wiring()};
    expect(codec.initialize() == Status::Ok, "the codec initializes");
    expect(codec.play(std::span<const mm::audio::Sample>{}) == Status::BadArgument,
           "an empty playback is not a transfer");
    expect(mm_test_es8311_i2s_size() == 0, "nothing is shipped in the refusal");
}

void shutdown_mutes_the_output() {
    mm_test_es8311_reset();
    mm::audio::es8311::Codec codec{wiring()};
    expect(codec.initialize() == Status::Ok, "the codec initializes");
    expect(codec.shutdown() == Status::Ok, "shutdown succeeds once initialized");
    expect(mm_test_es8311_write_count() == 6, "shutdown adds one register write");
    expect(mm_test_es8311_write(5) == (0x0a + 0x4cu * 0x100),
           "the output channel is muted on shutdown");
    expect(codec.shutdown() == Status::NotInitialized, "a second shutdown is not initialized");
}

const mm::test::case_ cases[] = {
    {"codec initializes through the reset and power-up", &initializes_through_the_reset_and_power_up},
    {"play ships sixteen-bit little-endian frames", &play_ships_sixteen_bit_little_endian_frames},
    {"play before initialize", &play_before_initialize_reports_not_initialized},
    {"codec rejects invalid wiring", &rejects_invalid_wiring},
    {"codec rejects an empty playback", &rejects_an_empty_playback},
    {"shutdown mutes the output", &shutdown_mutes_the_output},
};

const mm::test::registrar reg{"mm.audio.es8311", cases};

}  // namespace
