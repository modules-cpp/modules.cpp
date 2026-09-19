// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdint>
#include <fstream>
#include <optional>

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
    map.gpios.push_back({"/dev/gpiochip0", 18, "gpio.0"});
    map.gpios.push_back({"/dev/gpiochip0", 19, "gpio.1"});
    platform::linux::ParseError error;
    const mm::test::scoped_file good{
        "mm_linux_pwm_good.mdy",
        "pwm.0.chip = 0\n"
        "pwm.0.channel = 1\n"
        "pwm.0.gpio = 0\n"
        "pwm.0.name = \"backlight\"\n"
        "pwm.0.group = 3\n"
        "pwm.1.chip = 2\n"};
    expect(platform::linux::apply_override(map, good.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "PWM keys parse");
    expect(map.pwms.size() == 2 && map.pwms[0].chip == 0 && map.pwms[0].channel == 1 &&
               map.pwms[0].gpio == 0 && map.pwms[0].name == "backlight" &&
               map.pwms[0].group == 3,
           "an entry carries chip, channel, pad, name, and group");
    expect(map.pwms[1].name == "pwm.1" && !map.pwms[1].group && !map.pwms[1].gpio &&
               map.pwms[1].chip == 2 && map.pwms[1].channel == 0,
           "an entry's defaults are its index for a name, no group, no pad, channel zero");

    platform::linux::Map bad_pin;
    const mm::test::scoped_file pin{"mm_linux_pwm_pin.mdy", "pwm.0.gpio = 4\n"};
    expect(platform::linux::apply_override(bad_pin, pin.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError && error.key == "pwm.0.gpio",
           "a pad that is not a declared GPIO is refused");

    platform::linux::Map names;
    const mm::test::scoped_file duplicate{"mm_linux_pwm_names.mdy",
                                          "pwm.0.name = \"x\"\npwm.1.name = \"x\"\n"};
    expect(platform::linux::apply_override(names, duplicate.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "two outputs with one name are refused");

    platform::linux::Map unknown;
    const mm::test::scoped_file field{"mm_linux_pwm_field.mdy", "pwm.0.polarity = 1\n"};
    expect(platform::linux::apply_override(unknown, field.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "an unknown PWM field is refused");
}

// The registered map of this binary names no PWM output, so the live
// provider's answer is the honest one for a machine without the facility.
void provider_without_outputs_answers_unsupported() {
    expect(mm::mcu::pwm_description().outputs.empty(), "no outputs are described");
    std::uint64_t actual = 9;
    expect(mm::mcu::pwm_configure(0, 1000) == Status::Unsupported &&
               mm::mcu::pwm_period(0, actual) == Status::Unsupported && actual == 9 &&
               mm::mcu::pwm_write(0, 1) == Status::Unsupported &&
               mm::mcu::pwm_release(0) == Status::Unsupported,
           "every PWM call answers Unsupported and changes nothing");
    unsigned int output = 9;
    expect(mm::mcu::pwm_output_for_gpio(0, output) == Status::BadArgument && output == 9,
           "no pad finds an output");
}

const mm::test::case_ cases[] = {
    {"map keys parse and validate", &map_keys_parse_and_validate},
    {"provider without outputs answers Unsupported", &provider_without_outputs_answers_unsupported},
};

const mm::test::registrar reg{"platform.linux.mcu pwm", cases};

}  // namespace
