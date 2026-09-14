// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>
#include <vector>

import mm.mcu;
import mm.touch;
import mm.touch.cst328;
import mm.test;

void mm_test_touch_reset();
void mm_test_touch_force(mm::mcu::Status status);
void mm_test_touch_check_code(unsigned int value);
void mm_test_touch_points(unsigned int value);
void mm_test_touch_report(const unsigned char* bytes, std::size_t size);
std::size_t mm_test_touch_command_count();
unsigned int mm_test_touch_command(std::size_t index);
std::size_t mm_test_touch_read_count();
unsigned int mm_test_touch_read(std::size_t index);
std::size_t mm_test_touch_transactions();
unsigned long mm_test_touch_ticks();
std::size_t mm_test_touch_gpio_writes();
bool mm_test_touch_gpio_level(std::size_t index);

namespace {

using mm::test::expect;
using mm::touch::Status;

mm::touch::cst328::Wiring wiring() {
    return {.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
            .address = 0x1a,
            .reset_gpio = 17,
            .interrupt_gpio = 18,
            .reset_hold_ms = 10,
            .reset_release_ms = 50};
}

mm::touch::cst328::Panel panel() { return {.width = 240, .height = 320, .points = 5}; }

// A report as the controller lays one out: the first point at offset zero, and
// every later point two bytes further along because the flag register sits
// between them in the address space.
std::vector<unsigned char> report_with(std::vector<std::array<unsigned int, 2>> points) {
    std::vector<unsigned char> bytes(27, 0);
    bytes[0] = 0x06;  // the first point's identifier
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto base = index * 5 + (index > 0 ? 2 : 0);
        const auto x = points[index][0];
        const auto y = points[index][1];
        bytes[base + 1] = static_cast<unsigned char>(x >> 4);
        bytes[base + 2] = static_cast<unsigned char>(y >> 4);
        bytes[base + 3] = static_cast<unsigned char>(((x & 0x0f) << 4) | (y & 0x0f));
        bytes[base + 4] = 0x20;  // pressure, which mm.touch does not report
    }
    return bytes;
}

void initializes_through_the_identification_handshake() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "a recognised controller initializes");

    expect(mm_test_touch_gpio_writes() == 2 && !mm_test_touch_gpio_level(0) &&
               mm_test_touch_gpio_level(1),
           "reset is driven low and then released");
    expect(mm_test_touch_ticks() == 60, "both documented reset delays are observed");
    expect(mm_test_touch_command_count() == 2 && mm_test_touch_command(0) == 0xd101 &&
               mm_test_touch_command(1) == 0xd109,
           "debug-information mode is entered and normal mode restored");
    expect(mm_test_touch_read_count() == 1 && mm_test_touch_read(0) == 0xd1f4,
           "the identification block is read once");

    const auto geometry = controller.geometry();
    expect(geometry.width == 240 && geometry.height == 320 && geometry.points == 5,
           "geometry reports the panel the provider described");
}

void a_foreign_device_is_unsupported_and_left_in_normal_mode() {
    mm_test_touch_reset();
    mm_test_touch_check_code(0x1234);
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Unsupported,
           "a device that is not this controller is Unsupported");
    expect(mm_test_touch_command_count() == 2 && mm_test_touch_command(1) == 0xd109,
           "the device is returned to normal mode before the driver gives up");

    std::array<mm::touch::Point, 1> points{};
    std::size_t count = 0;
    expect(controller.read(points, count) == Status::NotInitialized,
           "a controller that did not initialize reports so rather than reading");
}

void no_contact_reads_as_ok_with_no_points() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    std::array<mm::touch::Point, 5> points{};
    std::size_t count = 7;
    expect(controller.read(points, count) == Status::Ok && count == 0,
           "nothing being touched is an answer rather than an error");
    expect(mm_test_touch_read(mm_test_touch_read_count() - 1) == 0xd005,
           "only the flag register is read when no point is reported");
}

void decodes_points_across_the_flag_register_gap() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const auto bytes = report_with({{{120, 200}}, {{31, 15}}});
    mm_test_touch_report(bytes.data(), bytes.size());
    mm_test_touch_points(2);

    std::array<mm::touch::Point, 5> points{};
    std::size_t count = 0;
    expect(controller.read(points, count) == Status::Ok && count == 2,
           "two reported contacts are read");
    expect(points[0].x == 120 && points[0].y == 200,
           "the first point decodes from its twelve-bit halves");
    expect(points[1].x == 31 && points[1].y == 15,
           "the second point decodes past the flag register gap");
}

void a_short_span_takes_what_it_has_room_for() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const auto bytes = report_with({{{10, 20}}, {{30, 40}}, {{50, 60}}});
    mm_test_touch_report(bytes.data(), bytes.size());
    mm_test_touch_points(3);

    std::array<mm::touch::Point, 1> points{};
    std::size_t count = 0;
    expect(controller.read(points, count) == Status::Ok && count == 1,
           "a single-touch caller reads one point from a multi-touch panel");
    expect(points[0].x == 10 && points[0].y == 20, "it is the first point");
}

void an_unsettled_report_yields_no_points() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    auto bytes = report_with({{{10, 20}}});
    bytes[0] = 0x00;  // the identifier the controller stamps only when settled
    mm_test_touch_report(bytes.data(), bytes.size());
    mm_test_touch_points(1);

    std::array<mm::touch::Point, 5> points{};
    std::size_t count = 9;
    expect(controller.read(points, count) == Status::Ok && count == 0,
           "a report that has not settled yields no position rather than a wrong one");
}

void every_register_access_is_one_transaction() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    const auto after_initialize = mm_test_touch_transactions();

    const auto bytes = report_with({{{1, 2}}});
    mm_test_touch_report(bytes.data(), bytes.size());
    mm_test_touch_points(1);
    std::array<mm::touch::Point, 5> points{};
    std::size_t count = 0;
    expect(controller.read(points, count) == Status::Ok, "a read succeeds");

    expect(mm_test_touch_transactions() == after_initialize + 2,
           "a contact costs one flag transaction and one report transaction");
}

void transport_failure_reaches_the_caller() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    mm_test_touch_force(mm::mcu::Status::Busy);

    std::array<mm::touch::Point, 1> points{};
    std::size_t count = 0;
    expect(controller.read(points, count) == Status::Busy,
           "a busy bus is reported as busy rather than as no contact");
    mm_test_touch_force(mm::mcu::Status::Ok);
}

void sleep_is_explicit_and_final() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");
    expect(controller.sleep() == Status::Ok, "the controller sleeps");
    expect(mm_test_touch_command(mm_test_touch_command_count() - 1) == 0xd105,
           "deep sleep is entered by its own register");

    std::array<mm::touch::Point, 1> points{};
    std::size_t count = 0;
    expect(controller.read(points, count) == Status::NotInitialized,
           "a sleeping controller requires reinitialization before it reads");
}

void a_panel_description_must_be_usable() {
    mm_test_touch_reset();
    mm::touch::cst328::Controller controller{wiring(), {.width = 0, .height = 0, .points = 0}};
    expect(controller.initialize() == Status::BadArgument,
           "a panel with no size and no points is rejected");
}

const mm::test::case_ cases[] = {
    {"initializes through identification", &initializes_through_the_identification_handshake},
    {"a foreign device is unsupported", &a_foreign_device_is_unsupported_and_left_in_normal_mode},
    {"no contact reads as no points", &no_contact_reads_as_ok_with_no_points},
    {"decodes points across the gap", &decodes_points_across_the_flag_register_gap},
    {"a short span takes what it can", &a_short_span_takes_what_it_has_room_for},
    {"an unsettled report yields none", &an_unsettled_report_yields_no_points},
    {"register access is one transaction", &every_register_access_is_one_transaction},
    {"transport failure reaches the caller", &transport_failure_reaches_the_caller},
    {"sleep is explicit and final", &sleep_is_explicit_and_final},
    {"a panel description must be usable", &a_panel_description_must_be_usable},
};

const mm::test::registrar reg{"mm.touch cst328", cases};

}  // namespace
