// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <fstream>

import mm.test;
import platform.linux.map;

namespace {

using mm::test::expect;

void override_inherits_and_decodes_values() {
    platform::linux::Map map;
    map.board_name = "default";
    map.rtc.allow_write = false;
    const mm::test::scoped_file file{
        "mm_linux_map_good.mdy",
        "# comment\nboard.name = \"workstation\\\" one\"\n"
        "rtc.allow-write = yes\n"
        "display.card = index:2\n"
        "spi.0.path = \"/dev/spidev0.0\"\n"
        "spi.0.clock-gpio = 11\n"
        "spi.0.transmit-gpio = 10\n"
        "spi.0.max-speed = 0xF4240\n"
        "spi.0.speed-fixed = no\n"
        "spi.0.mode = mode3\n"
        "spi.0.bit-order = lsb\n"};
    platform::linux::ParseError error;
    expect(platform::linux::apply_override(map, file.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "a well-formed override parses");
    expect(map.board_name == "workstation\" one" && map.rtc.allow_write,
           "quoted strings and booleans override defaults");
    expect(map.rtc.convention == platform::linux::RtcConvention::Utc,
           "an omitted field keeps its registered default");
    expect(map.display.card.kind == platform::linux::SelectorKind::Index &&
               map.display.card.index == 2,
           "index selectors parse");
    expect(map.spis.size() == 1 && map.spis[0].max_speed == 1'000'000 &&
               map.spis[0].mode == 3 && map.spis[0].least_significant_first,
           "SPI numeric and enum fields parse");
}

void duplicate_and_unknown_keys_are_errors() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    const mm::test::scoped_file duplicate{
        "mm_linux_map_duplicate.mdy",
        "board.name = \"one\"\nboard.name = \"two\"\n"};
    expect(platform::linux::apply_override(map, duplicate.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.line == 2 && error.previous_line == 1,
           "a duplicate names both source lines");
    const mm::test::scoped_file unknown{
        "mm_linux_map_unknown.mdy",
        "board.name = \"must-not-commit\"\nlinux.magic = yes\n"};
    map.board_name = "unchanged";
    error = {};
    expect(platform::linux::apply_override(map, unknown.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError && error.line == 2 &&
               error.key == "linux.magic",
           "an unknown key names its line and spelling");
    expect(map.board_name == "unchanged",
           "a failed override commits none of its earlier assignments");
}

void a_named_missing_file_is_an_error() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    expect(platform::linux::apply_override(
               map, "/tmp/mm-linux-map-deliberately-absent", error) ==
               platform::linux::MapStatus::FileError,
           "a named file is never silently ignored");
    expect(platform::linux::apply_override(map, "/dev/null", error) ==
               platform::linux::MapStatus::FileError,
           "a named path that is not a regular file is refused before it is read");
}

void gpio_inventory_and_led_validation() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    const mm::test::scoped_file invalid_led{
        "mm_linux_map_bad_led.mdy",
        "board.led.gpio = 3\n"
        "gpio.0.chip = \"/dev/gpiochip0\"\n"
        "gpio.0.offset = 12\n"};
    expect(platform::linux::apply_override(map, invalid_led.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "board.led.gpio",
           "board.led.gpio not in inventory is a map error");

    map = {};
    const mm::test::scoped_file valid_led{
        "mm_linux_map_good_led.mdy",
        "board.led.gpio = 0\n"
        "gpio.0.chip = \"/dev/gpiochip0\"\n"
        "gpio.0.offset = 12\n"};
    expect(platform::linux::apply_override(map, valid_led.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "board.led.gpio matching inventory is accepted");
    expect(map.gpios.size() == 1 && map.gpios[0].name == "gpio.0",
           "unnamed GPIO defaults to gpio.<n>");

    map = {};
    const mm::test::scoped_file dup_gpio{
        "mm_linux_map_dup_gpio.mdy",
        "gpio.0.chip = \"/dev/gpiochip0\"\n"
        "gpio.0.name = \"status\"\n"
        "gpio.1.chip = \"/dev/gpiochip0\"\n"
        "gpio.1.name = \"status\"\n"};
    expect(platform::linux::apply_override(map, dup_gpio.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "duplicate GPIO names are rejected");
}

const mm::test::case_ cases[]{
    {"override inherits and decodes", &override_inherits_and_decodes_values},
    {"duplicates and unknown keys", &duplicate_and_unknown_keys_are_errors},
    {"missing file errors", &a_named_missing_file_is_an_error},
    {"gpio inventory and led validation", &gpio_inventory_and_led_validation},
};
const mm::test::registrar reg{"platform.linux.map", cases};

}
