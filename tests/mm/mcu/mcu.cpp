// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
import mm.mcu;
import mm.test;

extern "C" {
extern int mm_test_forced_status;
extern unsigned long mm_test_ticks;
extern unsigned int mm_test_uart_instance;
extern const char* mm_test_uart_text;
void mm_test_reset();
void mm_test_set_level(unsigned int pin, int high);
}

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
    mm_test_set_level(3, 1);
    expect(mm::mcu::gpio_configure(3, Direction::In, Pull::None) == Status::Ok, "input configured");
    bool sensed = false;
    expect(mm::mcu::gpio_read(3, sensed) == Status::Ok && sensed,
           "a level the platform reports reads through");

    mm_test_reset();
    unsigned long ticks = 41;
    mm_test_forced_status = 3;
    expect(mm::mcu::ticks_ms(ticks) == Status::Busy, "a busy platform reports Busy");
    expect(ticks == 41, "a failed tick read does not write its output");
}

void uart_reports_an_absent_instance() {
    mm_test_reset();
    expect(mm::mcu::uart_write(0, "hello") == Status::Ok, "writing instance zero succeeds");
    expect(mm_test_uart_instance == 0 && mm_test_uart_text != nullptr,
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

// The five documented codes map one to one; anything else is a platform bug, and
// this layer reports it as BadArgument rather than inventing a sixth status that
// every caller would have to switch on.
void every_status_code_maps() {
    mm_test_reset();
    mm_test_forced_status = 0;
    expect(mm::mcu::delay_ms(1) == Status::Ok, "0 is Ok");
    mm_test_forced_status = 1;
    expect(mm::mcu::delay_ms(1) == Status::BadArgument, "1 is BadArgument");
    mm_test_forced_status = 2;
    expect(mm::mcu::delay_ms(1) == Status::Unsupported, "2 is Unsupported");
    mm_test_forced_status = 3;
    expect(mm::mcu::delay_ms(1) == Status::Busy, "3 is Busy");
    mm_test_forced_status = 4;
    expect(mm::mcu::delay_ms(1) == Status::Timeout, "4 is Timeout");
    mm_test_forced_status = 99;
    expect(mm::mcu::delay_ms(1) == Status::BadArgument, "an undefined code is BadArgument");
    mm_test_reset();
}

const mm::test::case_ cases[] = {
    {"gpio round trip", &gpio_round_trip},
    {"gpio rejects what the platform rejects", &gpio_rejects_what_the_platform_rejects},
    {"a failed read leaves its output alone", &a_failed_read_leaves_its_output_alone},
    {"uart reports an absent instance", &uart_reports_an_absent_instance},
    {"timer advances and reads back", &timer_advances_and_reads_back},
    {"every status code maps", &every_status_code_maps},
};

const mm::test::registrar reg{"mm.mcu", cases};

}  // namespace
