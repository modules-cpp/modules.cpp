// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
void mm_test_adc_set_count(unsigned int channel, unsigned int count);
bool mm_test_adc_claimed(unsigned int channel);
int mm_test_pin_owner(unsigned int pin);
bool mm_test_gpio_configured(unsigned int pin);

namespace {

using mm::mcu::AdcChannel;
using mm::mcu::Direction;
using mm::mcu::Edge;
using mm::mcu::Pull;
using mm::mcu::Status;
using mm::test::expect;

constexpr int owner_none = 0;
constexpr int owner_gpio = 1;
constexpr int owner_watched = 2;
constexpr int owner_adc = 3;

void description_reaches_the_caller() {
    mm_test_reset();
    const auto description = mm::mcu::adc_description();
    expect(description.channels.size() == 4, "the stand-in describes four channels");
    expect(description.channels[0].number == 0 && description.channels[0].name == "ADC0" &&
               description.channels[0].gpio == 26 && description.channels[0].bits == 12 &&
               description.channels[0].reference_millivolts == 3300,
           "a pin channel carries its number, name, pad, width, and reference");
    expect(!description.channels[3].gpio && description.channels[3].name == "TEMP",
           "an internal source has no pad");
    expect(description.channels[2].reference_millivolts == 0,
           "a channel with no known reference says zero");
}

void lookup_by_gpio() {
    mm_test_reset();
    unsigned int channel = 99;
    expect(mm::mcu::adc_channel_for_gpio(27, channel) == Status::Ok && channel == 1,
           "an attached pad finds its channel");
    channel = 99;
    expect(mm::mcu::adc_channel_for_gpio(5, channel) == Status::BadArgument && channel == 99,
           "a pad with no channel is refused and the output is untouched");
    expect(mm::mcu::adc_channel_for_gpio(200, channel) == Status::BadArgument && channel == 99,
           "a pad outside the inventory is refused");
}

void configure_read_release_round_trip() {
    mm_test_reset();
    mm_test_adc_set_count(0, 2048);
    unsigned int count = 7;
    expect(mm::mcu::adc_read(0, count) == Status::BadArgument && count == 7,
           "a read before configure is BadArgument and leaves the count alone");
    expect(mm::mcu::adc_configure(0) == Status::Ok, "a channel configures");
    expect(mm_test_pin_owner(26) == owner_adc, "the converter holds the pad");
    expect(mm::mcu::adc_configure(0) == Status::Ok, "configuring again is idempotent");
    expect(mm::mcu::adc_read(0, count) == Status::Ok && count == 2048, "a read answers the count");
    expect(mm::mcu::adc_release(0) == Status::Ok, "a channel releases");
    expect(mm_test_pin_owner(26) == owner_none && !mm_test_adc_claimed(0),
           "release leaves the pad unowned and the channel unclaimed");
    expect(mm::mcu::adc_release(0) == Status::Ok, "releasing again is idempotent");
    expect(mm::mcu::adc_read(0, count) == Status::BadArgument && count == 2048,
           "a read after release is BadArgument");
    expect(mm::mcu::adc_configure(9) == Status::BadArgument &&
               mm::mcu::adc_release(9) == Status::BadArgument,
           "a channel outside the inventory is BadArgument");
}

void internal_source_has_no_pad_to_own() {
    mm_test_reset();
    mm_test_adc_set_count(3, 876);
    expect(mm::mcu::adc_configure(3) == Status::Ok, "the internal channel configures");
    unsigned int count = 0;
    expect(mm::mcu::adc_read(3, count) == Status::Ok && count == 876,
           "the internal channel reads");
    expect(mm::mcu::adc_release(3) == Status::Ok && !mm_test_adc_claimed(3),
           "the internal channel releases");
}

void plain_gpio_yields_and_watched_gpio_does_not() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(26, Direction::Out, Pull::None) == Status::Ok &&
               mm::mcu::gpio_write(26, true) == Status::Ok,
           "the pad starts as a driven output");
    expect(mm::mcu::adc_configure(0) == Status::Ok, "the converter takes a plain GPIO over");
    expect(!mm_test_gpio_configured(26) && mm_test_pin_owner(26) == owner_adc,
           "the digital mode is gone");
    bool high = false;
    expect(mm::mcu::gpio_write(26, true) == Status::Busy &&
               mm::mcu::gpio_read(26, high) == Status::Busy && !high &&
               mm::mcu::gpio_configure(26, Direction::In, Pull::Up) == Status::Busy &&
               mm::mcu::gpio_watch(26, Pull::Up, Edge::Rising) == Status::Busy,
           "the GPIO facility is refused on a pad the converter holds");
    expect(mm::mcu::adc_release(0) == Status::Ok &&
               mm::mcu::gpio_configure(26, Direction::Out, Pull::None) == Status::Ok,
           "after release the pad is a GPIO again");

    mm_test_reset();
    expect(mm::mcu::gpio_watch(27, Pull::Up, Edge::Both) == Status::Ok, "a pad is watched");
    expect(mm::mcu::adc_configure(1) == Status::Busy, "a watched pad refuses the converter");
    expect(mm_test_pin_owner(27) == owner_watched && !mm_test_adc_claimed(1),
           "the watch stands and nothing was claimed");
    expect(mm::mcu::gpio_unwatch(27) == Status::Ok && mm::mcu::adc_configure(1) == Status::Ok,
           "once unwatched the pad can be claimed");
}

void a_failed_takeover_leaves_the_record_true() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(28, Direction::Out, Pull::None) == Status::Ok,
           "the pad is a GPIO");
    mm_test_force(Status::TransportError);
    expect(mm::mcu::adc_configure(2) == Status::TransportError, "the hardware refuses");
    mm_test_force(Status::Ok);
    expect(mm_test_pin_owner(28) == owner_gpio && mm_test_gpio_configured(28) &&
               !mm_test_adc_claimed(2),
           "the GPIO claim survives and the converter recorded nothing");
    expect(mm::mcu::gpio_write(28, true) == Status::Ok, "the pad still drives");
}

void every_status_reaches_the_caller() {
    mm_test_reset();
    expect(mm::mcu::adc_configure(0) == Status::Ok, "configured");
    unsigned int count = 5;
    for (const auto status : {Status::BadArgument, Status::Unsupported, Status::Busy,
                              Status::Timeout, Status::TransportError}) {
        mm_test_force(status);
        expect(mm::mcu::adc_read(0, count) == status && count == 5,
               "a refused read carries the platform's status and changes nothing");
        expect(mm::mcu::adc_configure(1) == status && mm::mcu::adc_release(0) == status,
               "configure and release carry the status");
    }
    mm_test_force(Status::Ok);
}

void millivolts_arithmetic() {
    const AdcChannel twelve{0, "x", std::nullopt, 12, 3300};
    expect(mm::mcu::adc_millivolts(twelve, 0) == 0, "zero is zero");
    expect(mm::mcu::adc_millivolts(twelve, 4095) == 3300, "full scale is the reference");
    expect(mm::mcu::adc_millivolts(twelve, 2048) == 1650,
           "the midpoint rounds to nearest: 2048 * 3300 / 4095 is 1650.4");
    expect(mm::mcu::adc_millivolts(twelve, 1) == 1, "one count is 0.806, rounded up");
    expect(mm::mcu::adc_millivolts(twelve, 4096) == 0, "a count past full scale is zero");
    const AdcChannel unknown{0, "x", std::nullopt, 12, 0};
    expect(mm::mcu::adc_millivolts(unknown, 4095) == 0, "no reference is zero");
    const AdcChannel wide{0, "x", std::nullopt, 32, 3300};
    const AdcChannel narrow{0, "x", std::nullopt, 0, 3300};
    expect(mm::mcu::adc_millivolts(wide, 1) == 0 && mm::mcu::adc_millivolts(narrow, 1) == 0,
           "bits outside [1, 31] are zero");
    const AdcChannel largest{0, "x", std::nullopt, 31, 0xffff'ffffu};
    expect(mm::mcu::adc_millivolts(largest, 0x7fff'ffffu) == 0xffff'ffffu,
           "the largest product still answers full scale");
    expect(mm::mcu::adc_millivolts(largest, 0x3fff'ffffu) == 0x7fff'fffeu,
           "half of the largest range: (2^30 - 1)(2^32 - 1) / (2^31 - 1) is 2^31 - 2");
    const AdcChannel one_bit{0, "x", std::nullopt, 1, 5000};
    expect(mm::mcu::adc_millivolts(one_bit, 1) == 5000, "one bit has one nonzero count");
}

void unserved_platform_defaults() {
    mm::mcu::Platform bare;
    expect(bare.adc_description().channels.empty(), "an unserved inventory is empty");
    unsigned int count = 3;
    expect(bare.adc_configure(0) == Status::Unsupported &&
               bare.adc_read(0, count) == Status::Unsupported && count == 3 &&
               bare.adc_release(0) == Status::Unsupported,
           "every ADC call defaults to Unsupported without changing output");
}

const mm::test::case_ cases[] = {
    {"description reaches the caller", &description_reaches_the_caller},
    {"lookup by gpio", &lookup_by_gpio},
    {"configure read release round trip", &configure_read_release_round_trip},
    {"internal source has no pad", &internal_source_has_no_pad_to_own},
    {"plain gpio yields and watched does not", &plain_gpio_yields_and_watched_gpio_does_not},
    {"a failed takeover leaves the record true", &a_failed_takeover_leaves_the_record_true},
    {"every status reaches the caller", &every_status_reaches_the_caller},
    {"millivolts arithmetic", &millivolts_arithmetic},
    {"unserved platform defaults", &unserved_platform_defaults},
};

const mm::test::registrar reg{"mm.mcu adc", cases};

}  // namespace
