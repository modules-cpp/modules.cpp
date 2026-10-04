// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>

import mm.mcu;
import mm.led;
import mm.led.ws2812b;
import mm.test;

void mm_test_led_reset();
void mm_test_led_force(mm::mcu::Status status);
mm::mcu::PulseConfiguration mm_test_led_configuration();
bool mm_test_led_configured();
unsigned int mm_test_led_configures();
unsigned int mm_test_led_releases();
std::size_t mm_test_led_frames();
std::size_t mm_test_led_frame_size(std::size_t index);
unsigned int mm_test_led_byte(std::size_t index, std::size_t offset);

namespace {

using mm::led::Color;
using mm::led::Status;
using mm::test::expect;
namespace ws = mm::led::ws2812b;

constexpr ws::Wiring wiring{.instance = 1, .gpio = 16};

std::array<std::byte, 9> frame;

ws::Chain chain(unsigned int count = 3, ws::Order order = ws::Order::Grb) {
    frame.fill(std::byte{0xa5});
    return {.count = count, .order = order, .frame = frame};
}

bool frame_is(std::size_t index, std::span<const unsigned int> bytes) {
    if (mm_test_led_frame_size(index) != bytes.size()) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (mm_test_led_byte(index, i) != bytes[i]) return false;
    return true;
}

void initialize_claims_the_datasheet_timing_and_goes_dark() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    expect(controller.count() == 3, "count is the chain's");
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const auto c = mm_test_led_configuration();
    expect(c.instance == 1 && c.gpio == 16, "the wiring reaches the pulse configuration");
    expect(c.bit_period_ns == 1'250 && c.zero_high_ns == 400 && c.one_high_ns == 800,
           "the bit timing is the datasheet's");
    expect(c.reset_ns == 280'000, "the reset time serves current parts as well as old");

    constexpr std::array<unsigned int, 9> dark{};
    expect(mm_test_led_frames() == 1 && frame_is(0, dark),
           "initialize sends one dark frame, whatever the provider's storage held");
}

void colours_go_out_in_grb_order() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const std::array<Color, 3> colours{{{0x11, 0x22, 0x33}, {0x44, 0x55, 0x66},
                                        {0x77, 0x88, 0x99}}};
    expect(controller.write(0, colours) == Status::Ok, "a whole-chain write succeeds");
    expect(mm_test_led_frames() == 1, "write sends nothing");
    expect(controller.refresh() == Status::Ok, "refresh sends the frame");

    constexpr std::array<unsigned int, 9> wire{0x22, 0x11, 0x33, 0x55, 0x44,
                                               0x66, 0x88, 0x77, 0x99};
    expect(mm_test_led_frames() == 2 && frame_is(1, wire),
           "each LED is green, red, blue on the wire");
}

void an_rgb_chain_keeps_rgb_order() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain(1, ws::Order::Rgb)};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    const std::array<Color, 1> colour{{{0x11, 0x22, 0x33}}};
    expect(controller.write(0, colour) == Status::Ok && controller.refresh() == Status::Ok,
           "an RGB chain writes and refreshes");
    constexpr std::array<unsigned int, 3> wire{0x11, 0x22, 0x33};
    expect(frame_is(1, wire), "an RGB clone is sent red, green, blue");
}

void a_write_at_an_offset_changes_only_its_leds() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    const std::array<Color, 1> colour{{{0x01, 0x02, 0x03}}};
    expect(controller.write(2, colour) == Status::Ok && controller.refresh() == Status::Ok,
           "the last LED alone is written");
    constexpr std::array<unsigned int, 9> wire{0, 0, 0, 0, 0, 0, 0x02, 0x01, 0x03};
    expect(frame_is(1, wire), "the LEDs before it stay as they were");
}

void a_write_must_fit_the_chain() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    const std::array<Color, 2> two{};
    expect(controller.write(2, two) == Status::BadArgument,
           "a run past the end of the chain is refused");
    expect(controller.write(4, std::span<const Color>{}) == Status::BadArgument,
           "a first LED past the end is refused");
    expect(controller.write(3, std::span<const Color>{}) == Status::Ok,
           "an empty run at the end is nothing to do");
}

void the_frame_must_hold_the_chain() {
    mm_test_led_reset();
    ws::Controller too_long{wiring, chain(4)};
    expect(too_long.initialize() == Status::BadArgument,
           "a frame shorter than three bytes an LED is refused");
    ws::Controller empty{wiring, chain(0)};
    expect(empty.initialize() == Status::BadArgument, "an empty chain is refused");
    expect(mm_test_led_configures() == 0, "neither claims the transport");
}

void clear_sends_the_chain_dark() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    const std::array<Color, 3> lit{{{9, 9, 9}, {9, 9, 9}, {9, 9, 9}}};
    expect(controller.write(0, lit) == Status::Ok, "the chain is lit");
    expect(controller.clear() == Status::Ok, "clear succeeds");
    constexpr std::array<unsigned int, 9> dark{};
    expect(mm_test_led_frames() == 2 && frame_is(1, dark), "clear refreshes a dark frame");
}

void the_lifecycle_is_explicit() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    const std::array<Color, 1> colour{};
    expect(controller.write(0, colour) == Status::NotInitialized,
           "a write before initialize is refused");
    expect(controller.refresh() == Status::NotInitialized,
           "so is a refresh");
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    expect(controller.sleep() == Status::Ok, "sleep succeeds");
    expect(!mm_test_led_configured() && mm_test_led_releases() == 1,
           "sleep returns the transport");
    constexpr std::array<unsigned int, 9> dark{};
    expect(frame_is(mm_test_led_frames() - 1, dark), "after sending the chain dark");
    expect(controller.sleep() == Status::Ok && mm_test_led_releases() == 1,
           "a second sleep is nothing to do");
    expect(controller.write(0, colour) == Status::NotInitialized,
           "a sleeping chain refuses writes");
    expect(controller.initialize() == Status::Ok && mm_test_led_configured(),
           "and initialize claims the transport again");
}

void a_transport_failure_stops_the_controller() {
    mm_test_led_reset();
    ws::Controller controller{wiring, chain()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    mm_test_led_force(mm::mcu::Status::Busy);
    expect(controller.refresh() == Status::Busy, "a failing transport is reported");
    mm_test_led_force(mm::mcu::Status::Ok);
    expect(controller.refresh() == Status::NotInitialized,
           "a controller whose LEDs are in an unknown state stops claiming to be ready");
    mm_test_led_reset();
    mm_test_led_force(mm::mcu::Status::Unsupported);
    ws::Controller unserved{wiring, chain()};
    expect(unserved.initialize() == Status::Unsupported,
           "a platform without the pulse facility is reported as such");
}

const mm::test::case_ cases[] = {
    {"initialize claims timing and goes dark", &initialize_claims_the_datasheet_timing_and_goes_dark},
    {"colours go out in GRB order", &colours_go_out_in_grb_order},
    {"an RGB chain keeps RGB order", &an_rgb_chain_keeps_rgb_order},
    {"a write at an offset", &a_write_at_an_offset_changes_only_its_leds},
    {"a write must fit the chain", &a_write_must_fit_the_chain},
    {"the frame must hold the chain", &the_frame_must_hold_the_chain},
    {"clear sends the chain dark", &clear_sends_the_chain_dark},
    {"the lifecycle is explicit", &the_lifecycle_is_explicit},
    {"transport failure stops it", &a_transport_failure_stops_the_controller},
};

const mm::test::registrar reg{"mm.led ws2812b", cases};

}  // namespace
