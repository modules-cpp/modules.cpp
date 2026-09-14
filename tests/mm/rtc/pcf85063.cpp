// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>

import mm.mcu;
import mm.rtc;
import mm.rtc.pcf85063;
import mm.test;

void mm_test_rtc_reset();
void mm_test_rtc_force(mm::mcu::Status status);
void mm_test_rtc_set_register(unsigned int reg, unsigned int value);
unsigned int mm_test_rtc_register(unsigned int reg);
std::size_t mm_test_rtc_transactions();
std::size_t mm_test_rtc_writes();
std::size_t mm_test_rtc_written_bytes();

namespace {

using mm::test::expect;
using mm::rtc::Status;

mm::rtc::pcf85063::Wiring wiring() {
    return {.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
            .address = 0x51};
}

// The calendar registers start at four: seconds, minutes, hours, days,
// weekdays, months, years.
void set_calendar(unsigned int second, unsigned int minute, unsigned int hour,
                  unsigned int day, unsigned int weekday, unsigned int month,
                  unsigned int year_digits) {
    mm_test_rtc_set_register(4, second);
    mm_test_rtc_set_register(5, minute);
    mm_test_rtc_set_register(6, hour);
    mm_test_rtc_set_register(7, day);
    mm_test_rtc_set_register(8, weekday);
    mm_test_rtc_set_register(9, month);
    mm_test_rtc_set_register(10, year_digits);
}

void reads_a_bcd_calendar_in_one_transaction() {
    mm_test_rtc_reset();
    mm::rtc::pcf85063::Clock clock{wiring()};
    expect(clock.initialize() == Status::Ok, "a clock that answers initializes");

    // 2026-09-14 17:45:09, a Monday by the part's own numbering.
    set_calendar(0x09, 0x45, 0x17, 0x14, 0x01, 0x09, 0x26);
    const auto before = mm_test_rtc_transactions();

    mm::rtc::DateTime time;
    bool trusted = false;
    expect(clock.read(time, trusted) == Status::Ok, "the calendar reads");
    expect(mm_test_rtc_transactions() == before + 1,
           "all seven fields come from one transaction");
    expect(time.year == 2026 && time.month == 9 && time.day == 14,
           "the date decodes from BCD with the century applied");
    expect(time.hour == 17 && time.minute == 45 && time.second == 9,
           "the time decodes from BCD");
    expect(time.weekday == 1, "the weekday decodes");
    expect(trusted, "a running clock reports a trusted reading");
}

void a_stopped_oscillator_is_readable_and_untrusted() {
    mm_test_rtc_reset();
    mm::rtc::pcf85063::Clock clock{wiring()};
    expect(clock.initialize() == Status::Ok, "the clock initializes");

    // Bit seven of the seconds register, alongside a valid 09 seconds.
    set_calendar(0x89, 0x45, 0x17, 0x14, 0x01, 0x09, 0x26);

    mm::rtc::DateTime time;
    bool trusted = true;
    expect(clock.read(time, trusted) == Status::Ok,
           "a clock that lost its oscillator still answers");
    expect(!trusted, "and says the reading cannot be trusted");
    expect(time.second == 9,
           "the flag bit is masked out rather than corrupting the seconds");
}

void writes_the_calendar_as_bcd_in_one_transaction() {
    mm_test_rtc_reset();
    mm::rtc::pcf85063::Clock clock{wiring()};
    expect(clock.initialize() == Status::Ok, "the clock initializes");

    const mm::rtc::DateTime time{.year = 2026,
                                 .month = 12,
                                 .day = 31,
                                 .weekday = 4,
                                 .hour = 23,
                                 .minute = 59,
                                 .second = 58};
    expect(clock.write(time) == Status::Ok, "a plausible calendar is accepted");
    expect(mm_test_rtc_writes() == 1 && mm_test_rtc_written_bytes() == 7,
           "the register address and all seven fields go in one transfer");
    expect(mm_test_rtc_register(4) == 0x58 && mm_test_rtc_register(5) == 0x59 &&
               mm_test_rtc_register(6) == 0x23,
           "the time is encoded as BCD");
    expect(mm_test_rtc_register(7) == 0x31 && mm_test_rtc_register(8) == 0x04 &&
               mm_test_rtc_register(9) == 0x12 && mm_test_rtc_register(10) == 0x26,
           "the date is encoded as BCD with the century removed");

    // Writing seconds clears the oscillator-stop bit, so the next reading is
    // trusted without a separate operation.
    mm::rtc::DateTime read_back;
    bool trusted = false;
    expect(clock.read(read_back, trusted) == Status::Ok && trusted,
           "a clock that was just set reads back trusted");
    expect(read_back.year == 2026 && read_back.month == 12 && read_back.day == 31 &&
               read_back.hour == 23 && read_back.minute == 59 && read_back.second == 58,
           "what was written is what reads back");
}

void an_unstorable_calendar_is_rejected_before_the_bus() {
    mm_test_rtc_reset();
    mm::rtc::pcf85063::Clock clock{wiring()};
    expect(clock.initialize() == Status::Ok, "the clock initializes");
    const auto writes = mm_test_rtc_writes();

    mm::rtc::DateTime time{.year = 2026, .month = 9, .day = 14,
                           .weekday = 1, .hour = 12, .minute = 0, .second = 0};

    time.year = 1999;
    expect(clock.write(time) == Status::BadArgument, "a year before the century is refused");
    time.year = 2100;
    expect(clock.write(time) == Status::BadArgument, "so is one after it");
    time.year = 2026;

    time.month = 13;
    expect(clock.write(time) == Status::BadArgument, "a thirteenth month is refused");
    time.month = 0;
    expect(clock.write(time) == Status::BadArgument, "so is a zeroth");
    time.month = 9;

    time.hour = 24;
    expect(clock.write(time) == Status::BadArgument, "a twenty-fourth hour is refused");
    time.hour = 12;
    time.second = 60;
    expect(clock.write(time) == Status::BadArgument, "so is a sixtieth second");

    expect(mm_test_rtc_writes() == writes,
           "a rejected calendar never reaches the part, so the clock is left alone");
}

void the_lifecycle_is_explicit() {
    mm_test_rtc_reset();
    mm::rtc::pcf85063::Clock clock{wiring()};

    mm::rtc::DateTime time;
    bool trusted = false;
    expect(clock.read(time, trusted) == Status::NotInitialized,
           "reading before initialization reports so");
    expect(clock.write(time) == Status::NotInitialized, "so does writing");
}

void transport_failure_reaches_the_caller() {
    mm_test_rtc_reset();
    mm::rtc::pcf85063::Clock clock{wiring()};
    expect(clock.initialize() == Status::Ok, "the clock initializes");
    mm_test_rtc_force(mm::mcu::Status::Timeout);

    mm::rtc::DateTime time;
    bool trusted = true;
    expect(clock.read(time, trusted) == Status::Timeout,
           "a timed-out bus is reported rather than returning a stale calendar");
    mm_test_rtc_force(mm::mcu::Status::Ok);
}

void an_unserved_platform_answers_unsupported() {
    mm::rtc::Clock bare;
    mm::rtc::DateTime time;
    bool trusted = false;
    expect(bare.initialize() == Status::Unsupported,
           "an unserved platform answers rather than failing to link");
    expect(bare.read(time, trusted) == Status::Unsupported, "so does a read");
    expect(bare.write(time) == Status::Unsupported, "so does a write");
}

const mm::test::case_ cases[] = {
    {"reads a BCD calendar", &reads_a_bcd_calendar_in_one_transaction},
    {"a stopped oscillator is untrusted", &a_stopped_oscillator_is_readable_and_untrusted},
    {"writes the calendar as BCD", &writes_the_calendar_as_bcd_in_one_transaction},
    {"an unstorable calendar is rejected", &an_unstorable_calendar_is_rejected_before_the_bus},
    {"the lifecycle is explicit", &the_lifecycle_is_explicit},
    {"transport failure reaches caller", &transport_failure_reaches_the_caller},
    {"unserved answers Unsupported", &an_unserved_platform_answers_unsupported},
};

const mm::test::registrar reg{"mm.rtc pcf85063", cases};

}  // namespace
