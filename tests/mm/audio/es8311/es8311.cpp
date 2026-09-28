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

namespace {

using mm::audio::Format;
using mm::audio::Ring;
using mm::audio::Status;
using mm::audio::es8311::Codec;
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

bool wrote(std::size_t index, unsigned int reg, unsigned int value) {
    return mm_test_es8311_write_register(index) == reg &&
           mm_test_es8311_write_value(index) == value;
}

// Brought up to the point a caller starts it: initialised, configured at
// 32 kHz, and a Ring of the actual format.
bool bring_up(Codec& codec, Ring& ring, std::span<std::int16_t> storage) {
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
    Codec codec{wiring()};
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
    Codec other{wiring()};
    expect(other.initialize() == Status::Unsupported, "a part that is not an ES8311 is refused");
    expect(mm_test_es8311_write_count() == 0, "and nothing is written to it");

    mm_test_es8311_reset();
    auto no_data = wiring();
    no_data.i2s.transmit_gpio.reset();
    Codec silent{no_data};
    expect(silent.initialize() == Status::BadArgument, "a link with no transmit line is refused");
    Codec wide24{wiring(24)};
    expect(wide24.initialize() == Status::BadArgument,
           "a twenty-four-bit slot has no whole clock multiplier and is refused");

    mm_test_es8311_reset();
    auto elsewhere = wiring();
    elsewhere.address = 0x19;
    Codec absent{elsewhere};
    expect(absent.initialize() == Status::BadArgument,
           "no answer at the wired address reaches the caller");

    mm_test_es8311_reset();
    mm_test_es8311_force(mm::mcu::Status::TransportError);
    Codec failing{wiring()};
    expect(failing.initialize() == Status::TransportError, "a bus failure reaches the caller");
}

void configure_starts_the_link_and_programs_the_clocks() {
    mm_test_es8311_reset();
    Codec codec{wiring()};
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
    Codec codec{wiring(32)};
    Format actual;
    expect(codec.initialize() == Status::Ok && wrote(13, 0x09, 0x50),
           "the serial port takes thirty-two-bit words");
    expect(codec.configure(rate_32k, actual) == Status::Ok && wrote(14, 0x02, 0x10),
           "and the bit clock is multiplied by four");
}

void start_checks_the_stream() {
    mm_test_es8311_reset();
    Codec codec{wiring()};
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
    Codec codec{wiring()};
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
    Codec codec{wiring(32)};
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
    Codec codec{wiring()};
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
    Codec codec{wiring()};
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
    Codec codec{wiring()};
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
    Codec codec{wiring()};
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
    Codec codec{wiring()};
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
    const Codec codec{wiring()};
    const auto description = codec.description();
    expect(description.name == "es8311" && description.rate_min_hz == 8'000 &&
               description.rate_max_hz == 96'000 &&
               description.depth_samples == Codec::scratch_frames,
           "the description names the part, its rates, and its own depth");
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
};

const mm::test::registrar reg{"mm.audio.es8311", cases};

}  // namespace
