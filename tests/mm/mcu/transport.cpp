// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The continuous transports -- paced ADC capture, the DAC, and I2S -- against
// the stand-in platform, which runs each transport's clock only when a case
// ticks it, so every sample period is one the case can see.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
bool mm_test_adc_claimed(unsigned int channel);
int mm_test_pin_owner(unsigned int pin);
void mm_test_adc_convert(std::uint16_t count);
void mm_test_dac_tick(unsigned int output, std::size_t periods);
unsigned int mm_test_dac_level(unsigned int output);
bool mm_test_i2s_ready();
void mm_test_i2s_tick(std::size_t frames);
void mm_test_i2s_receive(std::int32_t left, std::int32_t right);
std::int32_t mm_test_i2s_sent_left();
std::int32_t mm_test_i2s_sent_right();

namespace {

using mm::mcu::Direction;
using mm::mcu::Edge;
using mm::mcu::Frequency;
using mm::mcu::I2sConfiguration;
using mm::mcu::I2sDirection;
using mm::mcu::Progress;
using mm::mcu::Pull;
using mm::mcu::Status;
using mm::test::expect;

constexpr int owner_none = 0;
constexpr int owner_watched = 2;
constexpr int owner_adc = 3;
constexpr int owner_dac = 5;
constexpr int owner_i2s = 6;

// ---- paced ADC --------------------------------------------------------------

void paced_adc_limits_reach_the_caller() {
    mm_test_reset();
    const auto channels = mm::mcu::adc_description().channels;
    expect(channels[0].maximum_pace_hz == 500'000 && channels[0].pace_depth == 8,
           "a paceable channel states its fastest rate and its buffer");
    expect(channels[2].maximum_pace_hz == 0 && channels[3].maximum_pace_hz == 0,
           "a channel the platform cannot pace says zero");
}

void paced_adc_refuses_what_it_cannot_pace() {
    mm_test_reset();
    expect(mm::mcu::adc_pace(0, 0) == Status::BadArgument, "a zero rate is refused");
    expect(mm::mcu::adc_pace(0, 500'001) == Status::BadArgument,
           "a rate above the channel's maximum is refused");
    expect(mm::mcu::adc_pace(2, 8'000) == Status::BadArgument,
           "a channel that cannot be paced is refused");
    expect(mm::mcu::adc_pace(3, 8'000) == Status::BadArgument,
           "so is the internal source");
    expect(mm::mcu::adc_pace(9, 8'000) == Status::BadArgument,
           "a channel outside the inventory is refused");
    expect(!mm_test_adc_claimed(0) && !mm_test_adc_claimed(2) &&
               mm_test_pin_owner(26) == owner_none,
           "a refused pace claims nothing");
    expect(mm::mcu::gpio_watch(26, Pull::None, Edge::Rising) == Status::Ok,
           "the pad is watched");
    expect(mm::mcu::adc_pace(0, 8'000) == Status::Busy, "a watched pad cannot be paced");
    expect(mm_test_pin_owner(26) == owner_watched, "and keeps its watch");
}

// One multiplexer: while a channel is paced nothing else may convert, and the
// paced channel's own single reads would steal its conversions too.
void a_paced_channel_owns_the_whole_converter() {
    mm_test_reset();
    expect(mm::mcu::adc_configure(1) == Status::Ok, "a second channel is configured first");
    expect(mm::mcu::adc_pace(0, 32'000) == Status::Ok, "the first channel is paced");
    expect(mm_test_adc_claimed(0) && mm_test_pin_owner(26) == owner_adc,
           "pacing claims the channel's pad as adc_configure does");
    unsigned int count = 77;
    expect(mm::mcu::adc_read(1, count) == Status::Busy && count == 77,
           "a configured channel cannot read while another is paced");
    expect(mm::mcu::adc_read(0, count) == Status::Busy && count == 77,
           "nor can the paced channel itself");
    expect(mm::mcu::adc_configure(2) == Status::Busy,
           "no further channel is configured while one is paced");
    expect(mm::mcu::adc_pace(1, 32'000) == Status::Busy, "no second channel is paced");
    expect(mm::mcu::adc_release(0) == Status::Ok, "releasing the paced channel");
    expect(mm::mcu::adc_read(1, count) == Status::Ok,
           "gives the converter back to single reads");
    expect(mm::mcu::adc_configure(2) == Status::Ok && mm::mcu::adc_pace(1, 8'000) == Status::Ok,
           "and lets another channel be configured or paced");
}

void paced_adc_reports_its_exact_rate() {
    mm_test_reset();
    Frequency rate;
    expect(mm::mcu::adc_pace_rate(0, rate) == Status::BadArgument,
           "an unpaced channel has no rate");
    expect(mm::mcu::adc_pace(0, 32'000) == Status::Ok &&
               mm::mcu::adc_pace_rate(0, rate) == Status::Ok,
           "a paced channel reports its rate");
    expect(rate.numerator == 48'000'000 && rate.denominator == 1'500,
           "a rate the divider holds is exact");
    expect(mm::mcu::adc_pace(0, 44'100) == Status::Ok &&
               mm::mcu::adc_pace_rate(0, rate) == Status::Ok,
           "an idle channel may be paced again at another rate");
    expect(rate.numerator == 48'000'000 && rate.denominator == 1'088 &&
               rate.numerator != 44'100 * rate.denominator,
           "a rate the divider does not hold is reported as what runs");
}

void paced_adc_captures_only_after_start() {
    mm_test_reset();
    expect(mm::mcu::adc_pace(0, 8'000) == Status::Ok, "the channel is paced");
    mm_test_adc_convert(111);
    Progress progress;
    expect(mm::mcu::adc_pace_progress(0, progress) == Status::Ok &&
               progress.completed == 0 && progress.queued == 0,
           "an idle capture converts nothing");
    expect(mm::mcu::adc_pace_start(0) == Status::Ok, "capture starts");
    mm_test_adc_convert(1);
    mm_test_adc_convert(2);
    mm_test_adc_convert(3);
    std::uint16_t counts[2] = {};
    std::size_t count = 0;
    expect(mm::mcu::adc_take(0, counts, count) == Status::Ok && count == 2 &&
               counts[0] == 1 && counts[1] == 2,
           "a take returns counts oldest first, no more than the span holds");
    expect(mm::mcu::adc_pace_progress(0, progress) == Status::Ok &&
               progress.completed == 3 && progress.queued == 1 && progress.missed == 0,
           "progress counts every conversion and what is still buffered");
    expect(mm::mcu::adc_take(0, counts, count) == Status::Ok && count == 1 && counts[0] == 3,
           "the rest follows");
    count = 9;
    expect(mm::mcu::adc_take(0, counts, count) == Status::Ok && count == 0,
           "nothing waiting is an answer, not a failure");
    expect(mm::mcu::adc_take(1, counts, count) == Status::BadArgument,
           "an unpaced channel has nothing to take");
}

void paced_adc_drops_and_counts_what_it_cannot_buffer() {
    mm_test_reset();
    expect(mm::mcu::adc_pace(0, 8'000) == Status::Ok && mm::mcu::adc_pace_start(0) == Status::Ok,
           "capture starts");
    for (std::uint16_t i = 0; i < 10; ++i) mm_test_adc_convert(i);
    Progress progress;
    expect(mm::mcu::adc_pace_progress(0, progress) == Status::Ok &&
               progress.completed == 10 && progress.queued == 8 && progress.missed == 2,
           "conversions beyond the buffer are dropped and counted");
    std::uint16_t counts[8] = {};
    std::size_t count = 0;
    expect(mm::mcu::adc_take(0, counts, count) == Status::Ok && count == 8 &&
               counts[0] == 0 && counts[7] == 7,
           "the newest are the ones dropped");
}

void paced_adc_stop_discards_and_start_restarts() {
    mm_test_reset();
    expect(mm::mcu::adc_pace(0, 8'000) == Status::Ok && mm::mcu::adc_pace_start(0) == Status::Ok,
           "capture starts");
    mm_test_adc_convert(5);
    expect(mm::mcu::adc_pace(0, 16'000) == Status::Busy,
           "a started capture cannot change its rate");
    expect(mm::mcu::adc_pace_stop(0) == Status::Ok, "capture stops");
    mm_test_adc_convert(6);
    Progress progress;
    expect(mm::mcu::adc_pace_progress(0, progress) == Status::Ok && progress.queued == 0,
           "stop discards what was buffered, and a stopped capture converts nothing");
    expect(mm_test_adc_claimed(0), "a stopped channel stays claimed");
    unsigned int single = 0;
    expect(mm::mcu::adc_read(0, single) == Status::Busy,
           "and keeps the converter until release");
    expect(mm::mcu::adc_pace_start(0) == Status::Ok &&
               mm::mcu::adc_pace_progress(0, progress) == Status::Ok &&
               progress.completed == 0 && progress.missed == 0,
           "a restart begins its progress at zero");
}

void paced_adc_reports_every_status() {
    mm_test_reset();
    expect(mm::mcu::adc_pace(0, 8'000) == Status::Ok, "the channel is paced");
    mm_test_force(Status::TransportError);
    std::uint16_t counts[1] = {};
    std::size_t count = 42;
    expect(mm::mcu::adc_take(0, counts, count) == Status::TransportError && count == 42,
           "a transport error reaches the caller and leaves the count alone");
}

// ---- DAC --------------------------------------------------------------------

void dac_description_and_lookup_reach_the_caller() {
    mm_test_reset();
    const auto outputs = mm::mcu::dac_description().outputs;
    expect(outputs.size() == 2, "the stand-in describes two outputs");
    expect(outputs[0].number == 0 && outputs[0].name == "DAC0" && outputs[0].gpio == 20 &&
               outputs[0].bits == 10 && outputs[0].pwm && outputs[0].minimum_rate_hz == 8'000 &&
               outputs[0].maximum_rate_hz == 96'000 && outputs[0].depth == 4,
           "an output carries its number, name, pad, width, kind, limits, and depth");
    expect(!outputs[1].gpio && !outputs[1].pwm && outputs[1].maximum_rate_hz == 0,
           "a converter on no pad, with unknown limits, says so");
    unsigned int output = 99;
    expect(mm::mcu::dac_output_for_gpio(20, output) == Status::Ok && output == 0,
           "an attached pad finds its output");
    output = 99;
    expect(mm::mcu::dac_output_for_gpio(21, output) == Status::BadArgument && output == 99,
           "a pad with no output is refused and the output is untouched");
}

void dac_refuses_what_it_cannot_hold() {
    mm_test_reset();
    expect(mm::mcu::dac_configure(7, 8'000) == Status::BadArgument,
           "an output outside the inventory is refused");
    expect(mm::mcu::dac_configure(0, 0) == Status::BadArgument, "a zero rate is refused");
    expect(mm::mcu::dac_configure(0, 7'999) == Status::BadArgument &&
               mm::mcu::dac_configure(0, 96'001) == Status::BadArgument,
           "a rate outside the advertised limits is refused");
    expect(mm::mcu::dac_configure(1, 200'000) == Status::Ok,
           "unknown limits refuse nothing but zero");
    expect(mm_test_pin_owner(20) == owner_none, "a refused configure claims nothing");
    expect(mm::mcu::gpio_watch(20, Pull::None, Edge::Both) == Status::Ok, "the pad is watched");
    expect(mm::mcu::dac_configure(0, 8'000) == Status::Busy,
           "a watched pad cannot become a DAC");
    expect(mm_test_pin_owner(20) == owner_watched, "and keeps its watch");
}

void dac_claims_its_pad_and_holds_half_scale() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(20, Direction::Out, Pull::None) == Status::Ok,
           "the pad is a plain output first");
    expect(mm::mcu::dac_configure(0, 32'000) == Status::Ok, "the DAC takes it over");
    expect(mm_test_pin_owner(20) == owner_dac, "the pad belongs to the DAC");
    expect(mm::mcu::gpio_write(20, true) == Status::Busy,
           "and the GPIO facility cannot drive it behind the DAC's back");
    expect(mm::mcu::adc_configure(0) == Status::Ok, "an unrelated channel still configures");
    expect(mm_test_dac_level(0) == 512, "a configured output holds half scale");
    mm_test_dac_tick(0, 3);
    expect(mm_test_dac_level(0) == 512, "an idle output keeps holding half scale");
    Progress progress;
    expect(mm::mcu::dac_progress(0, progress) == Status::Ok && progress.missed == 0,
           "idling before start is not a miss");
}

void dac_reports_its_exact_rate() {
    mm_test_reset();
    Frequency rate;
    expect(mm::mcu::dac_rate(0, rate) == Status::BadArgument, "an unclaimed output has no rate");
    expect(mm::mcu::dac_configure(0, 44'100) == Status::Ok &&
               mm::mcu::dac_rate(0, rate) == Status::Ok,
           "a claimed output reports its rate");
    expect(rate.numerator == 1'000'000 && rate.denominator == 23,
           "the rate is the one the divider holds, exactly");
}

void dac_gives_accept_what_fits() {
    mm_test_reset();
    expect(mm::mcu::dac_configure(0, 8'000) == Status::Ok, "the output is claimed");
    const std::uint16_t levels[6] = {10, 20, 30, 40, 50, 60};
    std::size_t accepted = 99;
    expect(mm::mcu::dac_give(0, levels, accepted) == Status::Ok && accepted == 4,
           "a give accepts no more than the queue holds");
    expect(mm::mcu::dac_give(0, std::span{levels}.subspan(4), accepted) == Status::Ok &&
               accepted == 0,
           "a full queue accepts nothing, which is an answer");
    expect(mm::mcu::dac_stop(0) == Status::Ok, "stop empties the queue");
    const std::uint16_t wide[2] = {1, 1024};
    accepted = 99;
    expect(mm::mcu::dac_give(0, wide, accepted) == Status::BadArgument && accepted == 99,
           "a level outside the output's width is refused");
    Progress progress;
    expect(mm::mcu::dac_progress(0, progress) == Status::Ok && progress.queued == 0,
           "and nothing of the refused give was queued");
    expect(mm::mcu::dac_give(1, levels, accepted) == Status::BadArgument,
           "an unclaimed output accepts nothing");
}

// Silence is the transport's job: with nothing queued the output returns to
// half scale by itself, not to whatever level the program left it at.
void dac_plays_then_falls_silent_then_recovers() {
    mm_test_reset();
    expect(mm::mcu::dac_configure(0, 8'000) == Status::Ok, "the output is claimed");
    const std::uint16_t levels[2] = {1000, 900};
    std::size_t accepted = 0;
    expect(mm::mcu::dac_give(0, levels, accepted) == Status::Ok && accepted == 2,
           "levels queue before start");
    mm_test_dac_tick(0, 1);
    expect(mm_test_dac_level(0) == 512, "nothing plays before start");
    expect(mm::mcu::dac_start(0) == Status::Ok, "the output starts");
    mm_test_dac_tick(0, 1);
    expect(mm_test_dac_level(0) == 1000, "the first queued level plays");
    mm_test_dac_tick(0, 1);
    expect(mm_test_dac_level(0) == 900, "then the second");
    mm_test_dac_tick(0, 3);
    expect(mm_test_dac_level(0) == 512, "an empty queue plays half scale, not the last level");
    Progress progress;
    expect(mm::mcu::dac_progress(0, progress) == Status::Ok && progress.completed == 2 &&
               progress.missed == 3 && progress.queued == 0,
           "played levels and silent periods are counted apart");
    const std::uint16_t more[1] = {100};
    expect(mm::mcu::dac_give(0, more, accepted) == Status::Ok && accepted == 1,
           "a starved output accepts more");
    mm_test_dac_tick(0, 1);
    expect(mm_test_dac_level(0) == 100, "and plays it at the next period");
}

void dac_stop_discards_and_start_restarts() {
    mm_test_reset();
    expect(mm::mcu::dac_configure(0, 8'000) == Status::Ok && mm::mcu::dac_start(0) == Status::Ok,
           "the output starts");
    const std::uint16_t levels[3] = {7, 8, 9};
    std::size_t accepted = 0;
    expect(mm::mcu::dac_give(0, levels, accepted) == Status::Ok, "levels queue");
    mm_test_dac_tick(0, 1);
    expect(mm::mcu::dac_configure(0, 16'000) == Status::Busy,
           "a started output cannot change its rate");
    expect(mm::mcu::dac_stop(0) == Status::Ok, "the output stops");
    expect(mm_test_dac_level(0) == 512, "a stopped output returns to half scale");
    Progress progress;
    expect(mm::mcu::dac_progress(0, progress) == Status::Ok && progress.queued == 0,
           "stop discards the queue");
    expect(mm::mcu::dac_start(0) == Status::Ok &&
               mm::mcu::dac_progress(0, progress) == Status::Ok &&
               progress.completed == 0 && progress.missed == 0,
           "a restart begins its progress at zero");
    expect(mm::mcu::dac_release(0) == Status::Ok && mm_test_pin_owner(20) == owner_none,
           "release returns the pad");
    expect(mm::mcu::gpio_configure(20, Direction::Out, Pull::None) == Status::Ok,
           "which the GPIO facility can then configure");
}

// ---- I2S --------------------------------------------------------------------

// The Waveshare RP2350-Touch-LCD-2.8's wiring: bit clock, word clock, data out.
I2sConfiguration speaker_link() {
    I2sConfiguration configuration;
    configuration.instance = 0;
    configuration.bit_clock_gpio = 2;
    configuration.word_clock_gpio = 3;
    configuration.transmit_gpio = 4;
    configuration.rate_hz = 48'000;
    configuration.slot_bits = 16;
    return configuration;
}

// A microphone's: twenty-four bits in thirty-two-bit slots, data in.
I2sConfiguration microphone_link() {
    I2sConfiguration configuration;
    configuration.instance = 1;
    configuration.bit_clock_gpio = 10;
    configuration.word_clock_gpio = 11;
    configuration.receive_gpio = 12;
    configuration.rate_hz = 16'000;
    configuration.slot_bits = 32;
    return configuration;
}

void i2s_configuration_defaults_to_a_working_link() {
    const I2sConfiguration configuration;
    expect(configuration.rate_hz == 48'000 && configuration.slot_bits == 16,
           "a caller that names only pins gets 48 kHz sixteen-bit slots");
    expect(!configuration.transmit_gpio && !configuration.receive_gpio,
           "and names its data lines itself");
}

void i2s_refuses_what_it_cannot_carry() {
    mm_test_reset();
    auto invalid = speaker_link();
    invalid.rate_hz = 0;
    expect(mm::mcu::i2s_configure(invalid) == Status::BadArgument, "a zero rate is refused");
    invalid = speaker_link();
    invalid.slot_bits = 20;
    expect(mm::mcu::i2s_configure(invalid) == Status::BadArgument,
           "a slot width the platform does not carry is refused");
    invalid = speaker_link();
    invalid.transmit_gpio.reset();
    expect(mm::mcu::i2s_configure(invalid) == Status::BadArgument,
           "a link with no data line is refused");
    invalid = speaker_link();
    invalid.transmit_gpio = 2;
    expect(mm::mcu::i2s_configure(invalid) == Status::BadArgument,
           "one pin cannot carry two of the link's signals");
    invalid = speaker_link();
    invalid.instance = 2;
    expect(mm::mcu::i2s_configure(invalid) == Status::BadArgument,
           "an instance the platform lacks is refused");
    expect(!mm_test_i2s_ready() && mm_test_pin_owner(2) == owner_none,
           "a refused configure claims nothing");
    expect(mm::mcu::gpio_watch(4, Pull::None, Edge::Rising) == Status::Ok, "a data pad is watched");
    expect(mm::mcu::i2s_configure(speaker_link()) == Status::Busy,
           "a link over a held pad is Busy");
    expect(mm_test_pin_owner(2) == owner_none && mm_test_pin_owner(4) == owner_watched,
           "and claims none of its pads");
}

void i2s_configure_claims_pads_and_runs_its_clocks() {
    mm_test_reset();
    expect(mm::mcu::i2s_configure(speaker_link()) == Status::Ok, "the link configures");
    expect(mm_test_i2s_ready() && mm_test_pin_owner(2) == owner_i2s &&
               mm_test_pin_owner(3) == owner_i2s && mm_test_pin_owner(4) == owner_i2s,
           "the link claims both clocks and its data line");
    expect(mm::mcu::gpio_configure(3, Direction::Out, Pull::None) == Status::Busy,
           "a clock pad cannot be taken by the GPIO facility");
    expect(mm::mcu::i2s_configure(speaker_link()) == Status::Ok,
           "the same configuration again is idempotent");
    auto other = speaker_link();
    other.rate_hz = 44'100;
    expect(mm::mcu::i2s_configure(other) == Status::Busy,
           "a different configuration of a running link is Busy");
    Frequency rate;
    expect(mm::mcu::i2s_rate(0, rate) == Status::Ok && rate.numerator == 12'288'000 &&
               rate.denominator == 256,
           "a rate the divider holds is exact");
    mm_test_i2s_tick(2);
    expect(mm_test_i2s_sent_left() == 0 && mm_test_i2s_sent_right() == 0,
           "an idle transmitter sends zeros");
    Progress progress;
    expect(mm::mcu::i2s_progress(0, I2sDirection::Transmit, progress) == Status::Ok &&
               progress.missed == 0,
           "and idling before start is not a miss");
    expect(mm::mcu::i2s_start(0, I2sDirection::Receive) == Status::BadArgument,
           "a direction with no data line cannot start");
}

void i2s_words_match_the_slot_width() {
    mm_test_reset();
    expect(mm::mcu::i2s_configure(speaker_link()) == Status::Ok &&
               mm::mcu::i2s_start(0, I2sDirection::Transmit) == Status::Ok,
           "a sixteen-bit transmitter starts");
    const std::int32_t wide[2] = {1, 2};
    std::size_t accepted = 99;
    expect(mm::mcu::i2s_write(0, std::span<const std::int32_t>{wide}, accepted) ==
                   Status::BadArgument &&
               accepted == 99,
           "thirty-two-bit words on sixteen-bit slots are refused");
    const std::int16_t odd[3] = {1, 2, 3};
    expect(mm::mcu::i2s_write(0, std::span<const std::int16_t>{odd}, accepted) ==
               Status::BadArgument,
           "an odd word count is not whole frames");
    std::int16_t read[2] = {};
    expect(mm::mcu::i2s_read(0, std::span<std::int16_t>{read}, accepted) == Status::BadArgument,
           "a transmit-only link has nothing to read");
    expect(mm::mcu::i2s_write(1, std::span<const std::int16_t>{odd}.first(2), accepted) ==
               Status::BadArgument,
           "an instance not configured is refused");
}

void i2s_transmits_then_falls_silent_then_recovers() {
    mm_test_reset();
    expect(mm::mcu::i2s_configure(speaker_link()) == Status::Ok &&
               mm::mcu::i2s_start(0, I2sDirection::Transmit) == Status::Ok,
           "the transmitter starts");
    const std::int16_t words[10] = {1, -1, 2, -2, 3, -3, 4, -4, 5, -5};
    std::size_t accepted = 0;
    expect(mm::mcu::i2s_write(0, std::span<const std::int16_t>{words}.first(6), accepted) ==
                   Status::Ok &&
               accepted == 3,
           "three frames are queued");
    expect(mm::mcu::i2s_write(0, std::span<const std::int16_t>{words}.subspan(6), accepted) ==
                   Status::Ok &&
               accepted == 1,
           "only as many more as the buffer holds are accepted");
    mm_test_i2s_tick(1);
    expect(mm_test_i2s_sent_left() == 1 && mm_test_i2s_sent_right() == -1,
           "the first frame goes out left word first");
    Progress progress;
    expect(mm::mcu::i2s_progress(0, I2sDirection::Transmit, progress) == Status::Ok &&
               progress.completed == 1 && progress.queued == 3,
           "progress counts frames sent and frames waiting");
    mm_test_i2s_tick(5);
    expect(mm_test_i2s_sent_left() == 0 && mm_test_i2s_sent_right() == 0,
           "a starved transmitter sends zeros by itself");
    expect(mm::mcu::i2s_progress(0, I2sDirection::Transmit, progress) == Status::Ok &&
               progress.completed == 4 && progress.missed == 2,
           "and counts the frames it had nothing for");
    expect(mm::mcu::i2s_write(0, std::span<const std::int16_t>{words}.subspan(8), accepted) ==
                   Status::Ok &&
               accepted == 1,
           "a starved transmitter accepts more");
    mm_test_i2s_tick(1);
    expect(mm_test_i2s_sent_left() == 5, "and sends it at the next frame");
    expect(mm::mcu::i2s_stop(0, I2sDirection::Transmit) == Status::Ok && mm_test_i2s_ready(),
           "stop leaves the clocks running");
    mm_test_i2s_tick(1);
    expect(mm::mcu::i2s_progress(0, I2sDirection::Transmit, progress) == Status::Ok &&
               progress.queued == 0 && mm_test_i2s_sent_left() == 0,
           "a stopped transmitter has an empty buffer and sends zeros");
}

void i2s_receives_wide_slots() {
    mm_test_reset();
    expect(mm::mcu::i2s_configure(microphone_link()) == Status::Ok, "the receiver configures");
    mm_test_i2s_receive(9, 9);
    expect(mm::mcu::i2s_start(1, I2sDirection::Receive) == Status::Ok, "the receiver starts");
    for (std::int32_t i = 1; i <= 5; ++i) mm_test_i2s_receive(i << 8, -(i << 8));
    Progress progress;
    expect(mm::mcu::i2s_progress(1, I2sDirection::Receive, progress) == Status::Ok &&
               progress.completed == 5 && progress.queued == 4 && progress.missed == 1,
           "frames beyond the buffer are dropped and counted, and none before start arrive");
    std::int32_t words[4] = {};
    std::size_t count = 0;
    expect(mm::mcu::i2s_read(1, std::span<std::int32_t>{words}, count) == Status::Ok &&
               count == 2 && words[0] == 256 && words[1] == -256 && words[2] == 512,
           "a read returns whole frames, oldest first");
    std::int16_t narrow[2] = {};
    expect(mm::mcu::i2s_read(1, std::span<std::int16_t>{narrow}, count) == Status::BadArgument,
           "sixteen-bit words on thirty-two-bit slots are refused");
    const std::int32_t out[2] = {};
    expect(mm::mcu::i2s_write(1, std::span<const std::int32_t>{out}, count) ==
               Status::BadArgument,
           "a receive-only link cannot transmit");
    expect(mm::mcu::i2s_stop(1, I2sDirection::Receive) == Status::Ok &&
               mm::mcu::i2s_start(1, I2sDirection::Receive) == Status::Ok &&
               mm::mcu::i2s_progress(1, I2sDirection::Receive, progress) == Status::Ok &&
               progress.queued == 0 && progress.completed == 0,
           "a restart discards what was held and begins at zero");
}

void i2s_release_returns_the_pads() {
    mm_test_reset();
    expect(mm::mcu::i2s_configure(speaker_link()) == Status::Ok, "the link configures");
    expect(mm::mcu::i2s_release(0) == Status::Ok && !mm_test_i2s_ready(),
           "release stops the link");
    expect(mm_test_pin_owner(2) == owner_none && mm_test_pin_owner(4) == owner_none,
           "and returns its pads");
    expect(mm::mcu::gpio_configure(2, Direction::Out, Pull::None) == Status::Ok,
           "which the GPIO facility can then configure");
    auto other = speaker_link();
    other.rate_hz = 44'100;
    expect(mm::mcu::i2s_configure(other) == Status::Ok && mm_test_pin_owner(2) == owner_i2s,
           "a released instance configures afresh, taking over a plainly configured pad");
    Frequency rate;
    expect(mm::mcu::i2s_rate(0, rate) == Status::Ok && rate.denominator == 279,
           "at its new rate");
}

const mm::test::case_ cases[] = {
    {"paced adc limits reach the caller", &paced_adc_limits_reach_the_caller},
    {"paced adc refuses what it cannot pace", &paced_adc_refuses_what_it_cannot_pace},
    {"a paced channel owns the whole converter", &a_paced_channel_owns_the_whole_converter},
    {"paced adc reports its exact rate", &paced_adc_reports_its_exact_rate},
    {"paced adc captures only after start", &paced_adc_captures_only_after_start},
    {"paced adc drops and counts", &paced_adc_drops_and_counts_what_it_cannot_buffer},
    {"paced adc stop discards, start restarts", &paced_adc_stop_discards_and_start_restarts},
    {"paced adc reports every status", &paced_adc_reports_every_status},
    {"dac description and lookup", &dac_description_and_lookup_reach_the_caller},
    {"dac refuses what it cannot hold", &dac_refuses_what_it_cannot_hold},
    {"dac claims its pad and holds half scale", &dac_claims_its_pad_and_holds_half_scale},
    {"dac reports its exact rate", &dac_reports_its_exact_rate},
    {"dac gives accept what fits", &dac_gives_accept_what_fits},
    {"dac plays, falls silent, recovers", &dac_plays_then_falls_silent_then_recovers},
    {"dac stop discards, start restarts", &dac_stop_discards_and_start_restarts},
    {"i2s defaults to a working link", &i2s_configuration_defaults_to_a_working_link},
    {"i2s refuses what it cannot carry", &i2s_refuses_what_it_cannot_carry},
    {"i2s claims pads and runs its clocks", &i2s_configure_claims_pads_and_runs_its_clocks},
    {"i2s words match the slot width", &i2s_words_match_the_slot_width},
    {"i2s transmits, falls silent, recovers", &i2s_transmits_then_falls_silent_then_recovers},
    {"i2s receives wide slots", &i2s_receives_wide_slots},
    {"i2s release returns the pads", &i2s_release_returns_the_pads},
};

const mm::test::registrar reg{"mm.mcu transport", cases};

}  // namespace
