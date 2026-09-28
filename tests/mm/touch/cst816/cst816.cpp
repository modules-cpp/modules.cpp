// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>

import mm.mcu;
import mm.touch;
import mm.touch.cst816;
import mm.test;

void mm_test_touch816_reset();
void mm_test_touch816_force(mm::mcu::Status status);
void mm_test_touch816_chip_id(unsigned int value);
void mm_test_touch816_fingers(unsigned int value);
void mm_test_touch816_coordinate(unsigned int x_high, unsigned int x_low, unsigned int y_high,
                                 unsigned int y_low);
std::size_t mm_test_touch816_write_count();
unsigned int mm_test_touch816_write(std::size_t index);
std::size_t mm_test_touch816_read_count();
unsigned int mm_test_touch816_read(std::size_t index);
unsigned long mm_test_touch816_ticks();
std::size_t mm_test_touch816_gpio_writes();
bool mm_test_touch816_gpio_level(std::size_t index);

namespace {

using mm::test::expect;
using mm::touch::Status;

mm::touch::cst816::Wiring wiring() {
    return {.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
            .address = 0x15,
            .reset_gpio = 16,
            .interrupt_gpio = 15,
            .reset_hold_ms = 100,
            .reset_release_ms = 100};
}

mm::touch::cst816::Panel panel() { return {.width = 240, .height = 240, .points = 1}; }

void initializes_through_the_identification_handshake() {
    mm_test_touch816_reset();
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "a recognised controller initializes");

    expect(mm_test_touch816_gpio_writes() == 2 && !mm_test_touch816_gpio_level(0) &&
               mm_test_touch816_gpio_level(1),
           "reset is driven low and then released");
    expect(mm_test_touch816_ticks() == 200, "both documented reset delays are observed");
    expect(mm_test_touch816_read_count() == 1 && mm_test_touch816_read(0) == 0xa7,
           "the chip identifier register is read once");
    expect(mm_test_touch816_write_count() == 2 &&
               mm_test_touch816_write(0) == (0xf6 + 0x01u * 0x100) &&
               mm_test_touch816_write(1) == (0xfe + 0x41u * 0x100),
           "auto-sleep is disabled and point mode is selected");

    const auto geometry = controller.geometry();
    expect(geometry.width == 240 && geometry.height == 240 && geometry.points == 1,
           "geometry reports the panel the provider described");
}

void refuses_a_controller_that_answers_another_chip_id() {
    mm_test_touch816_reset();
    mm_test_touch816_chip_id(0x00);
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Unsupported,
           "an unrecognised answer at the address is not this controller");
}

void read_before_initialize_reports_not_initialized() {
    mm_test_touch816_reset();
    mm::touch::cst816::Controller controller{wiring(), panel()};
    mm::touch::Point point{};
    std::size_t count = 1;
    expect(controller.read({&point, 1}, count) == Status::NotInitialized,
           "no reading before the handshake");
}

void answers_no_contact_without_reading_a_point() {
    mm_test_touch816_reset();
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the handshake completes");

    mm_test_touch816_fingers(0);
    mm::touch::Point point{};
    std::size_t count = 1;
    expect(controller.read({&point, 1}, count) == Status::Ok, "no contact is an answer, not a fault");
    expect(count == 0, "no contact means no points");
    expect(mm_test_touch816_read_count() == 2 && mm_test_touch816_read(1) == 0x02,
           "the finger count alone is polled");
}

void decodes_the_reported_point() {
    mm_test_touch816_reset();
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the handshake completes");

    mm_test_touch816_fingers(1);
    mm_test_touch816_coordinate(0x00, 0x80, 0x00, 0x70);
    mm::touch::Point point{};
    std::size_t count = 0;
    expect(controller.read({&point, 1}, count) == Status::Ok, "one contact is read");
    expect(count == 1 && point.x == 0x80 && point.y == 0x70,
           "the coordinate pair decodes the high nibbles and the low bytes");
}

void clamps_the_reading_to_the_caller_span() {
    mm_test_touch816_reset();
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the handshake completes");

    mm_test_touch816_fingers(3);
    mm_test_touch816_coordinate(0x00, 0x80, 0x00, 0x70);
    std::size_t count = 1;
    expect(controller.read({}, count) == Status::Ok, "an empty span is still a call");
    expect(count == 0, "a caller with no room learns no points");
}

void sleep_enables_auto_sleep_and_stops_reporting() {
    mm_test_touch816_reset();
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the handshake completes");

    expect(controller.sleep() == Status::Ok, "sleep is requested");
    expect(mm_test_touch816_write_count() == 3 &&
               mm_test_touch816_write(2) == (0xf6 + 0x00u * 0x100),
           "auto-sleep is enabled by writing its register low");

    mm::touch::Point point{};
    std::size_t count = 1;
    expect(controller.read({&point, 1}, count) == Status::NotInitialized,
           "a sleeping controller does not report");
}

void a_transport_failure_is_not_a_timeout() {
    mm_test_touch816_reset();
    mm_test_touch816_force(mm::mcu::Status::TransportError);
    mm::touch::cst816::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::TransportError,
           "a bus failure is reported as the transport says");
    mm_test_touch816_force(mm::mcu::Status::Ok);
}

const mm::test::case_ cases[] = {
    {"initializes through the handshake", &initializes_through_the_identification_handshake},
    {"refuses another chip id", &refuses_a_controller_that_answers_another_chip_id},
    {"read before initialize", &read_before_initialize_reports_not_initialized},
    {"no contact is an answer", &answers_no_contact_without_reading_a_point},
    {"decodes the reported point", &decodes_the_reported_point},
    {"clamps to the caller span", &clamps_the_reading_to_the_caller_span},
    {"sleep stops reporting", &sleep_enables_auto_sleep_and_stops_reporting},
    {"transport failure", &a_transport_failure_is_not_a_timeout},
};

const mm::test::registrar reg{"mm.touch cst816", cases};

}  // namespace
