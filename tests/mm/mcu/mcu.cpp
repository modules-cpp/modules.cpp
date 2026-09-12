// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
void mm_test_set_level(unsigned int pin, bool high);
unsigned long mm_test_ticks();
unsigned int mm_test_uart_instance();
bool mm_test_uart_written();

namespace {

using mm::mcu::Direction;
using mm::mcu::Pull;
using mm::mcu::Status;
using mm::test::expect;

void gpio_round_trip() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(25, Direction::Out, Pull::None) == Status::Ok,
           "configuring an output pin succeeds");
    expect(mm::mcu::gpio_write(25, true) == Status::Ok, "writing a configured output succeeds");

    bool high = false;
    expect(mm::mcu::gpio_read(25, high) == Status::Ok && high,
           "reading back the level a write set");
    expect(mm::mcu::gpio_write(25, false) == Status::Ok, "clearing the level succeeds");
    expect(mm::mcu::gpio_read(25, high) == Status::Ok && !high, "the cleared level reads back");
}

void gpio_rejects_what_the_platform_rejects() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(99, Direction::Out, Pull::None) == Status::BadArgument,
           "a pin the platform does not have is BadArgument");
    expect(mm::mcu::gpio_write(7, true) == Status::BadArgument,
           "writing an unconfigured pin is BadArgument");
    expect(mm::mcu::gpio_configure(7, Direction::In, Pull::Up) == Status::Ok,
           "configuring an input succeeds");
    expect(mm::mcu::gpio_write(7, true) == Status::Unsupported,
           "writing an input is Unsupported, not a failure to report");
}

// The out parameter is the part an interface gets wrong by default: a caller that
// checks the status must not have to wonder whether the variable was touched.
void a_failed_read_leaves_its_output_alone() {
    mm_test_reset();
    bool high = true;
    expect(mm::mcu::gpio_read(99, high) == Status::BadArgument, "reading a bad pin fails");
    expect(high, "a failed read does not write its output");

    mm_test_reset();
    mm_test_set_level(3, true);
    expect(mm::mcu::gpio_configure(3, Direction::In, Pull::None) == Status::Ok, "input configured");
    bool sensed = false;
    expect(mm::mcu::gpio_read(3, sensed) == Status::Ok && sensed,
           "a level the platform reports reads through");

    mm_test_reset();
    unsigned long ticks = 41;
    mm_test_force(Status::Busy);
    expect(mm::mcu::ticks_ms(ticks) == Status::Busy, "a busy platform reports Busy");
    expect(ticks == 41, "a failed tick read does not write its output");
}

void uart_reports_an_absent_instance() {
    mm_test_reset();
    expect(mm::mcu::uart_write(0, "hello") == Status::Ok, "writing instance zero succeeds");
    expect(mm_test_uart_instance() == 0 && mm_test_uart_written(),
           "the platform observed the write");
    expect(mm::mcu::uart_write(4, "hello") == Status::Unsupported,
           "an instance the platform lacks is Unsupported");
    expect(mm::mcu::uart_write(0, nullptr) == Status::BadArgument,
           "a null string is the platform's BadArgument");
}

void timer_advances_and_reads_back() {
    mm_test_reset();
    unsigned long before = 1;
    expect(mm::mcu::ticks_ms(before) == Status::Ok && before == 0, "ticks start at the platform's zero");
    expect(mm::mcu::delay_ms(500) == Status::Ok, "delaying succeeds");
    expect(mm::mcu::delay_ms(250) == Status::Ok, "delaying again succeeds");

    unsigned long after = 0;
    expect(mm::mcu::ticks_ms(after) == Status::Ok && after == 750,
           "ticks advance by what was delayed");
}

// Every status a platform can answer reaches the caller unchanged. The interface
// is typed end to end now, so there is no undefined code for it to translate.
void every_status_reaches_the_caller() {
    mm_test_reset();
    mm_test_force(Status::Ok);
    expect(mm::mcu::delay_ms(1) == Status::Ok, "0 is Ok");
    mm_test_force(Status::BadArgument);
    expect(mm::mcu::delay_ms(1) == Status::BadArgument, "1 is BadArgument");
    mm_test_force(Status::Unsupported);
    expect(mm::mcu::delay_ms(1) == Status::Unsupported, "2 is Unsupported");
    mm_test_force(Status::Busy);
    expect(mm::mcu::delay_ms(1) == Status::Busy, "3 is Busy");
    mm_test_force(Status::Timeout);
    expect(mm::mcu::delay_ms(1) == Status::Timeout, "4 is Timeout");
    // an out-of-range code cannot be forged through a typed interface
    mm_test_reset();
}

// A platform overrides what it has and inherits Unsupported for the rest, so an
// interface may grow a facility without every platform growing with it.
void an_unserved_facility_answers_unsupported() {
    mm_test_reset();
    mm::mcu::Platform bare;
    expect(bare.gpio_write(0, true) == Status::Unsupported,
           "an unimplemented facility answers Unsupported rather than failing to link");
    expect(bare.uart_write(0, "x") == Status::Unsupported, "so does an unimplemented uart");
    expect(bare.delay_ms(1) == Status::Unsupported, "so does an unimplemented timer");
}

const mm::test::case_ cases[] = {
    {"gpio round trip", &gpio_round_trip},
    {"gpio rejects what the platform rejects", &gpio_rejects_what_the_platform_rejects},
    {"a failed read leaves its output alone", &a_failed_read_leaves_its_output_alone},
    {"uart reports an absent instance", &uart_reports_an_absent_instance},
    {"timer advances and reads back", &timer_advances_and_reads_back},
    {"every status reaches the caller", &every_status_reaches_the_caller},
    {"an unserved facility answers Unsupported", &an_unserved_facility_answers_unsupported},
};

const mm::test::registrar reg{"mm.mcu", cases};

}  // namespace
