// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

import mm.mcu;
import mm.audio;
import mm.audio.es8311;
import mm.test;

void mm_test_es8311_reset();
void mm_test_es8311_force(mm::mcu::Status status);
void mm_test_es8311_identity(unsigned int first, unsigned int second);
std::size_t mm_test_es8311_write_count();
unsigned int mm_test_es8311_write_register(std::size_t index);
unsigned int mm_test_es8311_write_value(std::size_t index);
unsigned long mm_test_es8311_ticks();
bool mm_test_es8311_link_configured();
unsigned long mm_test_es8311_link_rate();
unsigned int mm_test_es8311_link_configures();
unsigned int mm_test_es8311_link_releases();
bool mm_test_es8311_link_started();
void mm_test_es8311_tick(std::size_t frames);
void mm_test_es8311_accept(std::size_t limit);
void mm_test_es8311_accept_all();
void mm_test_es8311_write_error(mm::mcu::Status status);
std::size_t mm_test_es8311_sent_count();
std::int32_t mm_test_es8311_sent_left(std::size_t index);
std::int32_t mm_test_es8311_sent_right(std::size_t index);
std::size_t mm_test_es8311_queued();
void mm_test_es8311_receive(std::int32_t left, std::int32_t right);
bool mm_test_es8311_receiving();
std::size_t mm_test_es8311_received_waiting();
void mm_test_es8311_read_error(mm::mcu::Status status);

namespace {

using mm::audio::Format;
using mm::audio::Ring;
using mm::audio::Status;
using mm::audio::es8311::Codec;
using mm::audio::es8311::Input;
using mm::audio::es8311::Output;
using mm::test::expect;

constexpr Format rate_32k{.rate_hz = 32'000};

mm::audio::es8311::Wiring wiring(unsigned int slot_bits = 16) {
    mm::audio::es8311::Wiring result;
    result.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000};
    result.address = 0x18;
    result.i2s.instance = 0;
    result.i2s.bit_clock_gpio = 2;
    result.i2s.word_clock_gpio = 3;
    result.i2s.transmit_gpio = 4;
    result.i2s.slot_bits = slot_bits;
    result.reset_delay_ms = 5;
    return result;
}

// A microphone-only board: the link carries a receive line and no transmit.
mm::audio::es8311::Wiring microphone_wiring(unsigned int slot_bits = 16) {
    auto result = wiring(slot_bits);
    result.i2s.transmit_gpio.reset();
    result.i2s.receive_gpio = 5;
    return result;
}

// A board that plays and records through one codec.
mm::audio::es8311::Wiring duplex_wiring() {
    auto result = wiring();
    result.i2s.receive_gpio = 5;
    return result;
}

std::size_t count_writes(unsigned int reg, unsigned int value) {
    std::size_t found = 0;
    for (std::size_t i = 0; i < mm_test_es8311_write_count(); ++i)
        if (mm_test_es8311_write_register(i) == reg && mm_test_es8311_write_value(i) == value)
            ++found;
    return found;
}

bool wrote(std::size_t index, unsigned int reg, unsigned int value) {
    return mm_test_es8311_write_register(index) == reg &&
           mm_test_es8311_write_value(index) == value;
}

// Brought up to the point a caller starts it: initialised, configured at
// 32 kHz, and a Ring of the actual format.
bool bring_up(Output& codec, Ring& ring, std::span<std::int16_t> storage) {
    Format actual;
    return codec.initialize() == Status::Ok && codec.configure(rate_32k, actual) == Status::Ok &&
           ring.configure(storage, actual) == Status::Ok;
}

void the_default_address_is_the_parts() {
    const mm::audio::es8311::Wiring defaults;
    expect(defaults.address == 0x18, "the codec answers at 0x18 with CE low");
}

void initialize_identifies_resets_and_powers_up() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    expect(codec.initialize() == Status::Ok, "a codec at its address initialises");
    expect(mm_test_es8311_write_count() == 14, "the driver performs fourteen register writes");
    expect(wrote(0, 0x00, 0x1f) && wrote(1, 0x00, 0x80),
           "reset is entered, then left with the state machine on as a slave");
    expect(mm_test_es8311_ticks() == 5, "the reset pause is observed between the two");
    expect(wrote(2, 0x01, 0xbf), "the master clock comes from the bit clock, every clock on");
    expect(wrote(3, 0x0b, 0x00) && wrote(4, 0x0c, 0x00) && wrote(5, 0x10, 0x1f) &&
               wrote(6, 0x11, 0x7f),
           "the system and reference registers are set as the vendor sequence sets them");
    expect(wrote(7, 0x0d, 0x01) && wrote(8, 0x0e, 0x02),
           "the analog section powers up");
    expect(wrote(9, 0x12, 0x00) && wrote(10, 0x13, 0x10),
           "the DAC and its output driver power up");
    expect(wrote(11, 0x37, 0x08) && wrote(12, 0x32, 0xbf),
           "the DAC ramps with its equaliser bypassed, at unity volume");
    expect(wrote(13, 0x09, 0x4c),
           "the DAC's serial port, 0x09, is I2S sixteen-bit and muted until start");
    expect(!mm_test_es8311_link_configured(), "the link waits for configure");
}

void initialize_refuses_another_part_and_bad_wiring() {
    mm_test_es8311_reset();
    mm_test_es8311_identity(0x83, 0x10);
    Codec other_chip{wiring()};
    Output other{other_chip};
    expect(other.initialize() == Status::Unsupported, "a part that is not an ES8311 is refused");
    expect(mm_test_es8311_write_count() == 0, "and nothing is written to it");

    mm_test_es8311_reset();
    auto no_data = wiring();
    no_data.i2s.transmit_gpio.reset();
    Codec silent_chip{no_data};
    Output silent{silent_chip};
    expect(silent.initialize() == Status::BadArgument, "a link with no transmit line is refused");
    Codec wide24_chip{wiring(24)};
    Output wide24{wide24_chip};
    expect(wide24.initialize() == Status::BadArgument,
           "a twenty-four-bit slot has no whole clock multiplier and is refused");

    mm_test_es8311_reset();
    auto elsewhere = wiring();
    elsewhere.address = 0x19;
    Codec absent_chip{elsewhere};
    Output absent{absent_chip};
    expect(absent.initialize() == Status::BadArgument,
           "no answer at the wired address reaches the caller");

    mm_test_es8311_reset();
    mm_test_es8311_force(mm::mcu::Status::TransportError);
    Codec failing_chip{wiring()};
    Output failing{failing_chip};
    expect(failing.initialize() == Status::TransportError, "a bus failure reaches the caller");
}

void configure_starts_the_link_and_programs_the_clocks() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Format actual{.rate_hz = 7};
    expect(codec.configure(rate_32k, actual) == Status::NotInitialized && actual.rate_hz == 7,
           "configure waits for initialize");
    expect(codec.initialize() == Status::Ok, "initialised");
    expect(codec.configure({.rate_hz = 7'999}, actual) == Status::BadArgument &&
               codec.configure({.rate_hz = 96'001}, actual) == Status::BadArgument,
           "a rate outside eight to ninety-six kilohertz is refused");
    expect(!mm_test_es8311_link_configured(), "and starts nothing");
    expect(codec.configure(rate_32k, actual) == Status::Ok && actual.rate_hz == 32'000,
           "a rate the link holds is configured and reported");
    expect(mm_test_es8311_link_configured() && mm_test_es8311_link_rate() == 32'000,
           "the link runs at it");
    expect(wrote(14, 0x02, 0x18) && wrote(15, 0x03, 0x10) && wrote(16, 0x04, 0x10) &&
               wrote(17, 0x05, 0x00),
           "sixteen-bit slots multiply the bit clock by eight to 256 times the rate");
    mm::audio::Rate exact;
    expect(codec.rate(exact) == Status::Ok && exact.numerator == 12'288'000 &&
               exact.denominator == 384,
           "the exact rate is the link's");
    expect(codec.configure({.rate_hz = 44'100}, actual) == Status::Ok &&
               actual.rate_hz == 44'043 && mm_test_es8311_link_releases() == 1 &&
               mm_test_es8311_link_configures() == 2,
           "another rate while idle releases the link and configures it afresh");
}

void thirty_two_bit_slots_multiply_by_four() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring(32)};
    Output codec{codec_chip};
    Format actual;
    expect(codec.initialize() == Status::Ok && wrote(13, 0x09, 0x50),
           "the serial port takes thirty-two-bit words");
    expect(codec.configure(rate_32k, actual) == Status::Ok && wrote(14, 0x02, 0x10),
           "and the bit clock is multiplied by four");
}

void start_checks_the_stream() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[16] = {};
    expect(codec.start(ring) == Status::NotInitialized, "start waits for configure");
    expect(bring_up(codec, ring, storage), "brought up");
    Ring other;
    std::int16_t elsewhere[4] = {};
    expect(other.configure(elsewhere, {.rate_hz = 16'000}) == Status::Ok &&
               codec.start(other) == Status::BadArgument,
           "a Stream of another format is refused");
    expect(ring.claim(mm::audio::End::Consumer) == Status::Ok &&
               codec.start(ring) == Status::Busy,
           "a Stream whose consumer end is held is Busy");
    ring.release(mm::audio::End::Consumer);
    expect(codec.start(ring) == Status::Ok && mm_test_es8311_link_started(),
           "a matching Stream starts the transmitter");
    expect(wrote(18, 0x09, 0x0c), "and unmutes the serial port");
    expect(ring.claim(mm::audio::End::Consumer) == Status::Busy,
           "the codec holds the consumer end");
    expect(codec.start(ring) == Status::Busy, "a started codec cannot start again");
    Format actual;
    expect(codec.configure(rate_32k, actual) == Status::Busy,
           "nor be configured");
}

void each_sample_goes_to_both_slots() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[16] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    const std::int16_t samples[3] = {100, -200, 300};
    std::size_t count = 0;
    expect(ring.write(samples, count) == Status::Ok && count == 3, "three samples wait");
    expect(codec.service() == Status::Ok, "service moves them");
    mm_test_es8311_tick(3);
    expect(mm_test_es8311_sent_count() == 3 && mm_test_es8311_sent_left(0) == 100 &&
               mm_test_es8311_sent_right(0) == 100 && mm_test_es8311_sent_left(1) == -200 &&
               mm_test_es8311_sent_right(2) == 300,
           "each sample goes out in both slots, in order, unchanged");
    std::uint64_t position = 0;
    expect(codec.position(position) == Status::Ok && position == 3,
           "position counts what the link has sent");
}

void thirty_two_bit_words_are_widened() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring(32)};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[16] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    const std::int16_t samples[1] = {-2};
    std::size_t count = 0;
    expect(ring.write(samples, count) == Status::Ok && codec.service() == Status::Ok,
           "a sample is serviced");
    mm_test_es8311_tick(1);
    expect(mm_test_es8311_sent_left(0) == -2 * 65'536 && mm_test_es8311_sent_right(0) == -2 * 65'536,
           "a sample fills the upper sixteen bits of a thirty-two-bit slot");
}

// The transmitter may take part of what is offered, or none; what it did not
// take is offered again before anything more is read.
void a_pending_run_survives_partial_and_zero_acceptance() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[16] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    const std::int16_t samples[6] = {1, 2, 3, 4, 5, 6};
    std::size_t count = 0;
    expect(ring.write(samples, count) == Status::Ok && count == 6, "six samples wait");

    mm_test_es8311_accept(0);
    expect(codec.service() == Status::Ok, "a transmitter that takes nothing is not an error");
    std::size_t pending = 0;
    expect(ring.readable() == 0 && codec.pending(pending) == Status::Ok && pending == 6,
           "the six are the codec's pending run, not lost");

    mm_test_es8311_accept(2);
    expect(codec.service() == Status::Ok && mm_test_es8311_queued() == 2,
           "a partial acceptance queues two");
    expect(codec.pending(pending) == Status::Ok && pending == 6,
           "pending counts the run and the queue together");

    const std::int16_t more[2] = {7, 8};
    expect(ring.write(more, count) == Status::Ok && count == 2, "two more wait in the Ring");
    mm_test_es8311_accept(0);
    expect(codec.service() == Status::Ok && ring.readable() == 2,
           "the Ring is not read while a run is pending");
    mm_test_es8311_accept_all();
    mm_test_es8311_tick(2);
    expect(codec.service() == Status::Ok && mm_test_es8311_queued() == 4,
           "the rest of the run fills the transmitter");
    expect(ring.readable() == 0 && codec.pending(pending) == Status::Ok && pending == 6,
           "and only once it is gone is the Ring read, into a new run");
    mm_test_es8311_tick(4);
    expect(codec.service() == Status::Ok, "the new run is offered");
    mm_test_es8311_tick(2);
    bool ordered = mm_test_es8311_sent_count() == 8;
    for (std::size_t i = 0; i < mm_test_es8311_sent_count(); ++i)
        if (mm_test_es8311_sent_left(i) != static_cast<std::int32_t>(i + 1)) ordered = false;
    expect(ordered, "all eight arrive once, in order");
}

void an_error_keeps_the_pending_run() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[8] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    const std::int16_t samples[3] = {9, 10, 11};
    std::size_t count = 0;
    expect(ring.write(samples, count) == Status::Ok, "three samples wait");
    mm_test_es8311_write_error(mm::mcu::Status::TransportError);
    expect(codec.service() == Status::TransportError, "a transmitter error reaches the caller");
    std::size_t pending = 0;
    expect(codec.pending(pending) == Status::Ok && pending == 3,
           "and the run it could not hand over is still pending");
    mm_test_es8311_write_error(mm::mcu::Status::Ok);
    expect(codec.service() == Status::Ok, "the next service recovers");
    mm_test_es8311_tick(3);
    expect(mm_test_es8311_sent_count() == 3 && mm_test_es8311_sent_left(0) == 9 &&
               mm_test_es8311_sent_left(2) == 11,
           "with nothing lost or repeated");
}

void silence_is_reported_as_underrun() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[8] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    mm_test_es8311_tick(5);
    expect(codec.service() == Status::Ok && ring.counts().underrun_samples == 5,
           "five frames of transmitter silence are five samples of underrun");
    mm_test_es8311_tick(2);
    expect(codec.service() == Status::Ok && ring.counts().underrun_samples == 7,
           "only the growth is added each time");
    const std::int16_t samples[1] = {1};
    std::size_t count = 0;
    expect(ring.write(samples, count) == Status::Ok && codec.service() == Status::Ok,
           "a sample arrives");
    std::uint64_t position = 9;
    expect(codec.position(position) == Status::Ok && position == 0,
           "silence is not counted as position");
}

void stop_discards_below_the_stream_and_restart_resumes() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    Ring ring;
    std::int16_t storage[16] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    const std::int16_t samples[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::size_t count = 0;
    expect(ring.write(samples, count) == Status::Ok && count == 8, "eight samples wait");
    mm_test_es8311_accept(3);
    expect(codec.service() == Status::Ok, "three reach the transmitter, five are pending");
    const auto writes = mm_test_es8311_write_count();
    expect(codec.stop() == Status::Ok, "stop");
    expect(wrote(writes, 0x09, 0x4c), "mutes the serial port first");
    expect(!mm_test_es8311_link_started() && mm_test_es8311_queued() == 0,
           "then discards the transmitter's queue");
    expect(mm_test_es8311_link_configured(), "and leaves the clocks running");
    expect(ring.claim(mm::audio::End::Consumer) == Status::Ok,
           "the consumer end is released");
    ring.release(mm::audio::End::Consumer);
    std::size_t pending = 0;
    expect(codec.pending(pending) == Status::NotInitialized, "a stopped codec has no pending");
    expect(ring.readable() == 0, "what was read from the Stream was discarded, not returned");

    const std::int16_t next[2] = {20, 21};
    expect(ring.write(next, count) == Status::Ok, "more samples wait");
    mm_test_es8311_accept_all();
    expect(codec.start(ring) == Status::Ok && codec.service() == Status::Ok,
           "a restart resumes from the Stream");
    mm_test_es8311_tick(2);
    std::uint64_t position = 0;
    expect(mm_test_es8311_sent_left(0) == 20 && codec.position(position) == Status::Ok &&
               position == 2,
           "with position counted from the restart");
}

void sleep_mutes() {
    mm_test_es8311_reset();
    Codec codec_chip{wiring()};
    Output codec{codec_chip};
    expect(codec.sleep() == Status::NotInitialized, "an uninitialised codec cannot sleep");
    Ring ring;
    std::int16_t storage[4] = {};
    expect(bring_up(codec, ring, storage) && codec.start(ring) == Status::Ok, "started");
    const auto writes = mm_test_es8311_write_count();
    expect(codec.sleep() == Status::Ok && wrote(writes, 0x09, 0x4c) &&
               !mm_test_es8311_link_started(),
           "sleep stops a started codec, muted");
    expect(codec.sleep() == Status::Ok && wrote(writes + 1, 0x09, 0x4c),
           "and mutes an idle one");
}

void description_names_the_part() {
    Codec chip{wiring()};
    const Output codec{chip};
    const auto description = codec.description();
    expect(description.name == "es8311" && description.rate_min_hz == 8'000 &&
               description.rate_max_hz == 96'000 &&
               description.depth_samples == Output::scratch_frames,
           "the description names the part, its rates, and its own depth");
}


// ---- Input ------------------------------------------------------------------

bool bring_up_input(Input& input, Ring& ring, std::span<std::int16_t> storage) {
    Format actual;
    return input.initialize() == Status::Ok && input.configure(rate_32k, actual) == Status::Ok &&
           ring.configure(storage, actual) == Status::Ok;
}

void input_initialize_selects_the_microphone() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring()};
    Input input{chip};
    expect(input.initialize() == Status::Ok, "a codec with a receive line initialises for input");
    expect(mm_test_es8311_write_count() == 16, "the driver performs sixteen register writes");
    expect(wrote(0, 0x00, 0x1f) && wrote(1, 0x00, 0x80) && wrote(8, 0x0e, 0x02),
           "the shared reset and power-up come first");
    expect(wrote(9, 0x14, 0x1a), "the analog microphone is selected at thirty decibels");
    expect(wrote(10, 0x15, 0x40) && wrote(11, 0x16, 0x24) && wrote(12, 0x17, 0xbf),
           "the ADC ramps, scales, and runs at unity volume");
    expect(wrote(13, 0x1b, 0x0a) && wrote(14, 0x1c, 0x6a),
           "the high-pass filter removes the capsule's bias");
    expect(wrote(15, 0x0a, 0x4c),
           "the ADC's serial port, 0x0a, is I2S sixteen-bit and muted until start");
}

void input_refuses_bad_wiring() {
    mm_test_es8311_reset();
    Codec speaker_only{wiring()};
    Input deaf{speaker_only};
    expect(deaf.initialize() == Status::BadArgument, "a link with no receive line is refused");
    expect(mm_test_es8311_write_count() == 0, "and nothing is written");

    auto loud = microphone_wiring();
    loud.microphone_gain = 11;
    Codec loud_chip{loud};
    Input too_loud{loud_chip};
    expect(too_loud.initialize() == Status::BadArgument, "a gain past ten steps is refused");

    mm_test_es8311_reset();
    auto quiet = microphone_wiring();
    quiet.microphone_gain = 4;
    Codec quiet_chip{quiet};
    Input twelve_decibels{quiet_chip};
    expect(twelve_decibels.initialize() == Status::Ok && wrote(9, 0x14, 0x14),
           "the gain is the PGA's step");
}

void input_captures_its_slot_only_after_start() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring()};
    Input input{chip};
    Ring ring;
    std::int16_t storage[16] = {};
    expect(bring_up_input(input, ring, storage), "brought up");
    mm_test_es8311_receive(1, 2);
    expect(input.start(ring) == Status::Ok && mm_test_es8311_receiving(), "capture starts");
    expect(wrote(20, 0x0a, 0x0c), "the ADC's port is unmuted");
    expect(ring.claim(mm::audio::End::Producer) == Status::Busy,
           "the codec holds the producer end");
    mm_test_es8311_receive(100, -1);
    mm_test_es8311_receive(-200, -1);
    mm_test_es8311_receive(300, -1);
    expect(input.service() == Status::Ok, "service takes what arrived");
    std::int16_t out[8] = {};
    std::size_t count = 0;
    expect(ring.read(out, count) == Status::Ok && count == 3 && out[0] == 100 &&
               out[1] == -200 && out[2] == 300,
           "the left slot's words are the samples, in order, and nothing before start");
    std::uint64_t position = 0;
    expect(input.position(position) == Status::Ok && position == 3,
           "position counts what the receiver converted since start");
}

void input_reads_the_right_slot_when_wired_so() {
    mm_test_es8311_reset();
    auto right = microphone_wiring();
    right.input_slot = mm::audio::es8311::Slot::Right;
    Codec chip{right};
    Input input{chip};
    Ring ring;
    std::int16_t storage[4] = {};
    expect(bring_up_input(input, ring, storage) && input.start(ring) == Status::Ok, "started");
    mm_test_es8311_receive(7, 8);
    std::int16_t out[1] = {};
    std::size_t count = 0;
    expect(input.service() == Status::Ok && ring.read(out, count) == Status::Ok && count == 1 &&
               out[0] == 8,
           "the right slot's word is the sample");
}

void input_narrows_thirty_two_bit_words() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring(32)};
    Input input{chip};
    Ring ring;
    std::int16_t storage[4] = {};
    expect(bring_up_input(input, ring, storage) && wrote(15, 0x0a, 0x50),
           "the ADC's port takes thirty-two-bit words");
    expect(input.start(ring) == Status::Ok, "started");
    mm_test_es8311_receive(0x12345678, 0);
    mm_test_es8311_receive(-65'536 * 3, 0);
    std::int16_t out[2] = {};
    std::size_t count = 0;
    expect(input.service() == Status::Ok && ring.read(out, count) == Status::Ok && count == 2 &&
               out[0] == 0x1234 && out[1] == -3,
           "a thirty-two-bit word's upper sixteen bits are the sample");
}

// The Stream's room bounds what is taken, so the driver never holds a sample
// it could not write; what waits stays in the receiver.
void input_takes_no_more_than_the_stream_holds() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring()};
    Input input{chip};
    Ring ring;
    std::int16_t storage[4] = {};
    expect(bring_up_input(input, ring, storage) && input.start(ring) == Status::Ok, "started");
    for (std::int32_t i = 1; i <= 4; ++i) mm_test_es8311_receive(i, 0);
    expect(input.service() == Status::Ok && ring.readable() == 4, "four fill the Ring");
    for (std::int32_t i = 5; i <= 7; ++i) mm_test_es8311_receive(i, 0);
    expect(input.service() == Status::Ok && mm_test_es8311_received_waiting() == 3,
           "with the Ring full nothing is taken, and three wait in the receiver");
    std::int16_t out[8] = {};
    std::size_t count = 0;
    expect(ring.read(std::span<std::int16_t>{out}.first(2), count) == Status::Ok && count == 2,
           "the application reads two");
    expect(input.service() == Status::Ok && mm_test_es8311_received_waiting() == 1,
           "two more are taken, one still waits");
    expect(ring.read(out, count) == Status::Ok && count == 4 && out[0] == 3 && out[3] == 6,
           "and every sample arrives once, in order");
    expect(ring.counts().overrun_samples == 0, "nothing was lost");
}

void receiver_drops_are_reported_as_overrun() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring()};
    Input input{chip};
    Ring ring;
    std::int16_t storage[2] = {};
    expect(bring_up_input(input, ring, storage) && input.start(ring) == Status::Ok, "started");
    mm_test_es8311_receive(1, 0);
    mm_test_es8311_receive(2, 0);
    expect(input.service() == Status::Ok && ring.writable() == 0, "the Ring fills");
    for (std::int32_t i = 3; i <= 8; ++i) mm_test_es8311_receive(i, 0);
    expect(input.service() == Status::Ok && ring.counts().overrun_samples == 2,
           "six frames into a receiver that holds four drop two, reported as overrun");
    expect(input.service() == Status::Ok && ring.counts().overrun_samples == 2,
           "a service with no new drops adds nothing");
    for (std::int32_t i = 9; i <= 11; ++i) mm_test_es8311_receive(i, 0);
    expect(input.service() == Status::Ok && ring.counts().overrun_samples == 5,
           "three more drops add three");
    std::uint64_t position = 0;
    expect(input.position(position) == Status::Ok && position == 11,
           "position counts the dropped frames too");
}

void an_input_error_reaches_the_caller() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring()};
    Input input{chip};
    Ring ring;
    std::int16_t storage[4] = {};
    expect(bring_up_input(input, ring, storage) && input.start(ring) == Status::Ok, "started");
    mm_test_es8311_receive(9, 0);
    mm_test_es8311_read_error(mm::mcu::Status::TransportError);
    expect(input.service() == Status::TransportError && ring.readable() == 0,
           "a receiver error reaches the caller and writes nothing");
    mm_test_es8311_read_error(mm::mcu::Status::Ok);
    expect(input.service() == Status::Ok && ring.readable() == 1,
           "the frame is still there for the next service");
}

void input_stop_keeps_the_stream_and_restart_discards_the_receiver() {
    mm_test_es8311_reset();
    Codec chip{microphone_wiring()};
    Input input{chip};
    Ring ring;
    std::int16_t storage[8] = {};
    expect(bring_up_input(input, ring, storage) && input.start(ring) == Status::Ok, "started");
    mm_test_es8311_receive(1, 0);
    mm_test_es8311_receive(2, 0);
    expect(input.service() == Status::Ok, "two are captured");
    mm_test_es8311_receive(3, 0);
    const auto writes = mm_test_es8311_write_count();
    expect(input.stop() == Status::Ok && wrote(writes, 0x0a, 0x4c) && !mm_test_es8311_receiving(),
           "stop mutes the port and stops the receiver");
    expect(ring.readable() == 2, "the Stream keeps what was captured");
    expect(ring.claim(mm::audio::End::Producer) == Status::Ok, "the producer end is released");
    ring.release(mm::audio::End::Producer);
    expect(input.start(ring) == Status::Ok && input.service() == Status::Ok &&
               ring.readable() == 2,
           "a restart discards what the receiver held before it");
    std::uint64_t position = 9;
    expect(input.position(position) == Status::Ok && position == 0,
           "and counts position from the restart");
}

void input_description_names_the_part() {
    Codec chip{microphone_wiring()};
    const Input input{chip};
    const auto description = input.description();
    expect(description.name == "es8311" && description.rate_min_hz == 8'000 &&
               description.rate_max_hz == 96'000 &&
               description.depth_samples == Input::scratch_frames,
           "the description names the part, its rates, and its depth");
}

// ---- One chip, two directions -------------------------------------------------

void both_directions_share_one_chip_and_one_link() {
    mm_test_es8311_reset();
    Codec chip{duplex_wiring()};
    Output speaker{chip};
    Input microphone{chip};
    expect(speaker.initialize() == Status::Ok && microphone.initialize() == Status::Ok,
           "both directions initialise");
    expect(count_writes(0x00, 0x1f) == 1, "the chip is reset once, not under the other");
    expect(mm_test_es8311_write_count() == 21, "the shared, DAC, and ADC writes each happen once");
    Format played;
    Format recorded;
    expect(speaker.configure(rate_32k, played) == Status::Ok &&
               microphone.configure(rate_32k, recorded) == Status::Ok && played == recorded,
           "both configure at one rate");
    expect(mm_test_es8311_link_configures() == 1, "on one link, configured once");

    Ring in_ring;
    Ring out_ring;
    std::int16_t in_storage[8] = {};
    std::int16_t out_storage[8] = {};
    expect(in_ring.configure(in_storage, recorded) == Status::Ok &&
               out_ring.configure(out_storage, played) == Status::Ok,
           "a Stream for each");
    expect(microphone.start(in_ring) == Status::Ok, "the microphone starts");
    expect(speaker.configure({.rate_hz = 48'000}, played) == Status::Busy,
           "the speaker cannot move the link's rate while the microphone runs");
    expect(speaker.start(out_ring) == Status::Ok, "but it can start at the shared rate");
    expect(microphone.stop() == Status::Ok && speaker.stop() == Status::Ok, "both stop");

    expect(speaker.configure({.rate_hz = 48'000}, played) == Status::Ok &&
               mm_test_es8311_link_releases() == 1 && played.rate_hz == 48'000,
           "with both idle the link moves to the new rate");
    expect(microphone.start(in_ring) == Status::NotInitialized,
           "the microphone, configured at the old rate, must configure again");
    expect(microphone.configure({.rate_hz = 48'000}, recorded) == Status::Ok &&
               mm_test_es8311_link_configures() == 2,
           "configuring it at the link's rate touches nothing");
}

const mm::test::case_ cases[] = {
    {"the default address is the part's", &the_default_address_is_the_parts},
    {"initialize identifies, resets, powers up", &initialize_identifies_resets_and_powers_up},
    {"initialize refuses another part and bad wiring",
     &initialize_refuses_another_part_and_bad_wiring},
    {"configure starts the link, programs the clocks",
     &configure_starts_the_link_and_programs_the_clocks},
    {"thirty-two-bit slots multiply by four", &thirty_two_bit_slots_multiply_by_four},
    {"start checks the stream", &start_checks_the_stream},
    {"each sample goes to both slots", &each_sample_goes_to_both_slots},
    {"thirty-two-bit words are widened", &thirty_two_bit_words_are_widened},
    {"a pending run survives partial and zero acceptance",
     &a_pending_run_survives_partial_and_zero_acceptance},
    {"an error keeps the pending run", &an_error_keeps_the_pending_run},
    {"silence is reported as underrun", &silence_is_reported_as_underrun},
    {"stop discards below the stream, restart resumes",
     &stop_discards_below_the_stream_and_restart_resumes},
    {"sleep mutes", &sleep_mutes},
    {"description names the part", &description_names_the_part},
    {"input initialize selects the microphone", &input_initialize_selects_the_microphone},
    {"input refuses bad wiring", &input_refuses_bad_wiring},
    {"input captures its slot only after start", &input_captures_its_slot_only_after_start},
    {"input reads the right slot when wired so", &input_reads_the_right_slot_when_wired_so},
    {"input narrows thirty-two-bit words", &input_narrows_thirty_two_bit_words},
    {"input takes no more than the stream holds", &input_takes_no_more_than_the_stream_holds},
    {"receiver drops are reported as overrun", &receiver_drops_are_reported_as_overrun},
    {"an input error reaches the caller", &an_input_error_reaches_the_caller},
    {"input stop keeps the stream, restart discards the receiver",
     &input_stop_keeps_the_stream_and_restart_discards_the_receiver},
    {"input description names the part", &input_description_names_the_part},
    {"both directions share one chip and one link",
     &both_directions_share_one_chip_and_one_link},
};

const mm::test::registrar reg{"mm.audio.es8311", cases};

}  // namespace
