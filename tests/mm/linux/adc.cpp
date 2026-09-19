// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdint>
#include <fstream>
#include <optional>
#include <string_view>

import mm.mcu;
import mm.test;
import platform.linux.map;
import platform.linux.defaults;
import platform.linux.mcu;

namespace {

using mm::mcu::Status;
using mm::test::expect;

void map_keys_parse_and_validate() {
    platform::linux::Map map;
    map.gpios.push_back({"/dev/gpiochip0", 4, "gpio.0"});
    map.gpios.push_back({"/dev/gpiochip0", 5, "gpio.1"});
    platform::linux::ParseError error;
    const mm::test::scoped_file good{
        "mm_linux_adc_good.mdy",
        "adc.device = \"ads1015\"\n"
        "adc.0.channel = 3\n"
        "adc.0.gpio = 1\n"
        "adc.0.name = \"battery\"\n"
        "adc.0.bits = 11\n"
        "adc.0.reference-mv = 4096\n"
        "adc.1.channel = 0\n"};
    expect(platform::linux::apply_override(map, good.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "ADC keys parse");
    expect(map.adc_device.kind == platform::linux::SelectorKind::Name &&
               map.adc_device.name == "ads1015",
           "the device selector parses");
    expect(map.adcs.size() == 2 && map.adcs[0].channel == 3 && map.adcs[0].gpio == 1 &&
               map.adcs[0].name == "battery" && map.adcs[0].bits == 11 &&
               map.adcs[0].reference_millivolts == 4096,
           "an entry carries channel, pad, name, width, and reference");
    expect(map.adcs[1].name == "adc.1" && map.adcs[1].bits == 12 &&
               map.adcs[1].reference_millivolts == 0 && !map.adcs[1].gpio,
           "an entry's defaults are its index for a name, twelve bits, no reference, no pad");

    platform::linux::Map bad_bits;
    const mm::test::scoped_file bits{"mm_linux_adc_bits.mdy", "adc.0.bits = 32\n"};
    expect(platform::linux::apply_override(bad_bits, bits.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "bits outside [1, 31] are refused");

    platform::linux::Map bad_pin;
    const mm::test::scoped_file pin{"mm_linux_adc_pin.mdy", "adc.0.gpio = 7\n"};
    expect(platform::linux::apply_override(bad_pin, pin.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError && error.key == "adc.0.gpio",
           "a pad that is not a declared GPIO is refused");

    platform::linux::Map twice;
    twice.gpios.push_back({"/dev/gpiochip0", 4, "gpio.0"});
    const mm::test::scoped_file duplicate{"mm_linux_adc_twice.mdy",
                                          "adc.0.gpio = 0\nadc.1.gpio = 0\n"};
    expect(platform::linux::apply_override(twice, duplicate.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "a pad attached to two channels is refused");

    platform::linux::Map unknown;
    const mm::test::scoped_file field{"mm_linux_adc_field.mdy", "adc.0.scale = 1\n"};
    expect(platform::linux::apply_override(unknown, field.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "an unknown ADC field is refused");
}

void scale_parses_to_nanovolts() {
    using platform::linux::mcu_detail::adc_scale_nanovolts;
    std::uint64_t nanovolts = 1;
    expect(adc_scale_nanovolts("0.805664062\n", nanovolts) && nanovolts == 805'664,
           "a Pico-like scale of 0.8057 mV per count is 805,664 nV, the seventh digit rounded");
    expect(adc_scale_nanovolts("1.220703125", nanovolts) && nanovolts == 1'220'703,
           "a 4.096 V over twelve bits scale parses");
    expect(adc_scale_nanovolts("3", nanovolts) && nanovolts == 3'000'000,
           "an integer scale is whole millivolts");
    expect(adc_scale_nanovolts("0.5", nanovolts) && nanovolts == 500'000,
           "a short fraction is padded");
    expect(adc_scale_nanovolts("0.0000005", nanovolts) && nanovolts == 1,
           "half a nanovolt rounds up");
    nanovolts = 7;
    expect(!adc_scale_nanovolts("", nanovolts) && !adc_scale_nanovolts("-1.0", nanovolts) &&
               !adc_scale_nanovolts("1.", nanovolts) && !adc_scale_nanovolts(".5", nanovolts) &&
               !adc_scale_nanovolts("1e-3", nanovolts) && !adc_scale_nanovolts("abc", nanovolts) &&
               nanovolts == 7,
           "an empty, signed, dangling, bare-fraction, exponent, or non-numeric scale is refused");
}

void reference_derives_from_scale() {
    using platform::linux::mcu_detail::adc_reference_millivolts;
    expect(adc_reference_millivolts(805'664, 12) == 3299,
           "0.805664 mV times 4095 counts is 3,299 mV");
    expect(adc_reference_millivolts(1'220'703, 12) == 4999,
           "1.220703 mV times 4095 counts is 4,999 mV");
    expect(adc_reference_millivolts(1'000'000, 1) == 1, "one bit, one millivolt per count");
    expect(adc_reference_millivolts(0, 12) == 0 && adc_reference_millivolts(1'000, 0) == 0 &&
               adc_reference_millivolts(1'000, 32) == 0,
           "no scale or a width outside [1, 31] is zero");
    expect(adc_reference_millivolts(0xffff'ffff'ffff'ffffull, 31) == 0,
           "a product past 64 bits is zero rather than wrong");
}

void sysfs_numbers_parse() {
    using platform::linux::mcu_detail::sysfs_integer;
    long long value = 5;
    expect(sysfs_integer("2048\n", value) && value == 2048, "a raw count with its newline");
    expect(sysfs_integer("-12", value) && value == -12, "a negative value parses, for the caller to refuse");
    expect(sysfs_integer("1000000", value) && value == 1'000'000, "a period reads back");
    value = 5;
    expect(!sysfs_integer("", value) && !sysfs_integer("\n", value) &&
               !sysfs_integer("12a", value) && !sysfs_integer("0x10", value) && value == 5,
           "empty, blank, or non-decimal text is refused and the value untouched");
}

// The registered map of this binary names no ADC, so the live provider's
// answer is the honest one for a machine without the facility.
void provider_without_channels_answers_unsupported() {
    expect(mm::mcu::adc_description().channels.empty(), "no channels are described");
    unsigned int count = 9;
    expect(mm::mcu::adc_configure(0) == Status::Unsupported &&
               mm::mcu::adc_read(0, count) == Status::Unsupported && count == 9 &&
               mm::mcu::adc_release(0) == Status::Unsupported,
           "every ADC call answers Unsupported and changes nothing");
    unsigned int channel = 9;
    expect(mm::mcu::adc_channel_for_gpio(0, channel) == Status::BadArgument && channel == 9,
           "no pad finds a channel");
}

const mm::test::case_ cases[] = {
    {"map keys parse and validate", &map_keys_parse_and_validate},
    {"scale parses to nanovolts", &scale_parses_to_nanovolts},
    {"reference derives from scale", &reference_derives_from_scale},
    {"sysfs numbers parse", &sysfs_numbers_parse},
    {"provider without channels answers Unsupported", &provider_without_channels_answers_unsupported},
};

const mm::test::registrar reg{"platform.linux.mcu adc", cases};

}  // namespace
