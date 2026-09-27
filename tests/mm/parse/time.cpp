// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string_view>

import mm.parse;
import mm.test;

namespace {

using mm::parse::TimeKind;
using mm::parse::TimeValue;
using mm::parse::DateTime;
using mm::parse::Duration;
using mm::parse::parse_time;
using mm::test::expect;

// --- ISO date tests ---

void test_iso_date() {
    const auto result = parse_time("2026-09-27");
    expect(result.kind == TimeKind::Date, "ISO date kind");
    expect(result.date.year == 2026, "ISO year");
    expect(result.date.month == 9, "ISO month");
    expect(result.date.day == 27, "ISO day");
    expect(result.length == 10, "ISO date length");
}

void test_iso_date_leap_year() {
    const auto result = parse_time("2028-02-29");
    expect(result.kind == TimeKind::Date, "leap year Feb 29");
    expect(result.date.year == 2028, "leap year");
    expect(result.date.month == 2, "leap month");
    expect(result.date.day == 29, "leap day");
}

void test_iso_date_non_leap_feb_29_rejected() {
    const auto result = parse_time("2026-02-29");
    expect(result.kind == TimeKind::Invalid, "non-leap Feb 29 rejected");
}

void test_iso_date_bad_month() {
    const auto result = parse_time("2026-13-01");
    expect(result.kind == TimeKind::Invalid, "month 13 rejected");
}

void test_iso_date_bad_day() {
    const auto result = parse_time("2026-04-31");
    expect(result.kind == TimeKind::Invalid, "April 31 rejected");
}

void test_iso_date_year_zero() {
    const auto result = parse_time("0000-01-01");
    expect(result.kind == TimeKind::Date, "year 0 accepted");
    expect(result.date.year == 0, "year 0 value");
}

void test_iso_date_year_9999() {
    const auto result = parse_time("9999-12-31");
    expect(result.kind == TimeKind::Date, "year 9999 accepted");
    expect(result.date.year == 9999, "year 9999 value");
}

// --- Slash date tests ---

void test_slash_date() {
    const auto result = parse_time("2026/09/27");
    expect(result.kind == TimeKind::Date, "slash date kind");
    expect(result.date.year == 2026, "slash year");
    expect(result.date.month == 9, "slash month");
    expect(result.date.day == 27, "slash day");
}

// --- US date tests ---

void test_us_date() {
    const auto result = parse_time("09/27/2026");
    expect(result.kind == TimeKind::Date, "US date kind");
    expect(result.date.year == 2026, "US year");
    expect(result.date.month == 9, "US month");
    expect(result.date.day == 27, "US day");
}

void test_us_date_invalid() {
    const auto result = parse_time("13/27/2026");
    expect(result.kind == TimeKind::Invalid, "US month 13 rejected");
}

// --- ISO datetime tests ---

void test_iso_datetime_T() {
    const auto result = parse_time("2026-09-27T14:30:00");
    expect(result.kind == TimeKind::DateTime, "ISO datetime T kind");
    expect(result.date.year == 2026, "datetime year");
    expect(result.date.hour == 14, "datetime hour");
    expect(result.date.minute == 30, "datetime minute");
    expect(result.date.second == 0, "datetime second");
}

void test_iso_datetime_space() {
    const auto result = parse_time("2026-09-27 14:30:00");
    expect(result.kind == TimeKind::DateTime, "ISO datetime space kind");
    expect(result.date.hour == 14, "datetime space hour");
}

void test_iso_datetime_bad_hour() {
    const auto result = parse_time("2026-09-27T25:00:00");
    expect(result.kind == TimeKind::Date, "hour 25: falls back to Date");
}

// --- Time-only tests ---

void test_time_three_fields() {
    const auto result = parse_time("14:30:00");
    expect(result.kind == TimeKind::Time, "time 3 fields kind");
    expect(result.date.hour == 14, "time hour");
    expect(result.date.minute == 30, "time minute");
    expect(result.date.second == 0, "time second");
}

void test_time_two_fields() {
    const auto result = parse_time("14:30");
    expect(result.kind == TimeKind::Time, "time 2 fields kind");
    expect(result.date.hour == 14, "time 2 fields hour");
    expect(result.date.minute == 30, "time 2 fields minute");
    expect(result.date.second == 0, "time 2 fields second is 0");
}

void test_time_bad_hour() {
    const auto result = parse_time("25:00:00");
    expect(result.kind == TimeKind::Invalid, "hour 25 rejected");
}

void test_time_bad_minute() {
    const auto result = parse_time("14:60:00");
    expect(result.kind == TimeKind::Invalid, "minute 60 rejected");
}

// --- Epoch tests ---

void test_epoch_basic() {
    const auto result = parse_time("1759872000");
    expect(result.kind == TimeKind::Epoch, "epoch kind");
    expect(result.epoch == 1759872000, "epoch value");
    // 1759872000 = 2026-09-27 (verify date conversion)
    expect(result.date.year >= 2026, "epoch year >= 2026");
}

void test_epoch_zero() {
    const auto result = parse_time("0000000000");
    expect(result.kind == TimeKind::Epoch, "epoch 0 kind");
    expect(result.epoch == 0, "epoch 0 value");
    expect(result.date.year == 1970, "epoch 0 is 1970");
    expect(result.date.month == 1, "epoch 0 is January");
    expect(result.date.day == 1, "epoch 0 is the 1st");
}

void test_epoch_leap_year() {
    // 2028-02-29 12:00:00 UTC
    const auto result = parse_time("1739817600");
    expect(result.kind == TimeKind::Epoch, "leap year epoch");
    expect(result.date.year == 2028, "leap year epoch year");
    expect(result.date.month == 2, "leap year epoch month");
    expect(result.date.day == 29, "leap year epoch day");
}

void test_epoch_too_few_digits() {
    const auto result = parse_time("12345");
    expect(result.kind == TimeKind::Invalid, "5-digit number is not epoch");
}

void test_epoch_overflow() {
    const auto result = parse_time("999999999999");
    expect(result.kind == TimeKind::Epoch, "large epoch kind");
    expect(result.overflow, "large epoch overflows");
}

// --- Duration tests ---

void test_duration_seconds() {
    const auto result = parse_time("30s");
    expect(result.kind == TimeKind::Duration, "30s kind");
    expect(result.duration.value == 30, "30s value");
    expect(result.duration.unit == Duration::Unit::Seconds, "30s unit");
}

void test_duration_minutes() {
    const auto result = parse_time("5m");
    expect(result.kind == TimeKind::Duration, "5m kind");
    expect(result.duration.value == 5, "5m value");
    expect(result.duration.unit == Duration::Unit::Minutes, "5m unit");
}

void test_duration_hours() {
    const auto result = parse_time("2h");
    expect(result.kind == TimeKind::Duration, "2h kind");
    expect(result.duration.value == 2, "2h value");
    expect(result.duration.unit == Duration::Unit::Hours, "2h unit");
}

void test_duration_days() {
    const auto result = parse_time("1d");
    expect(result.kind == TimeKind::Duration, "1d kind");
    expect(result.duration.value == 1, "1d value");
    expect(result.duration.unit == Duration::Unit::Days, "1d unit");
}

void test_duration_weeks() {
    const auto result = parse_time("2w");
    expect(result.kind == TimeKind::Duration, "2w kind");
    expect(result.duration.value == 2, "2w value");
    expect(result.duration.unit == Duration::Unit::Weeks, "2w unit");
}

// --- Invalid tests ---

void test_invalid_empty() {
    const auto result = parse_time("");
    expect(result.kind == TimeKind::Invalid, "empty is Invalid");
}

void test_invalid_whitespace() {
    const auto result = parse_time("   ");
    expect(result.kind == TimeKind::Invalid, "whitespace is Invalid");
}

void test_invalid_mixed_separators() {
    const auto result = parse_time("2026-09/27");
    expect(result.kind == TimeKind::Invalid, "mixed separators rejected");
}

void test_invalid_no_year() {
    const auto result = parse_time("27-09-2026");
    expect(result.kind == TimeKind::Invalid, "no 4-digit year rejected");
}

// --- parse_time(text, at) tests ---

void test_parse_at_offset() {
    const std::string_view text = "date=2026-09-27;";
    const auto result = parse_time_at(text, 5);
    expect(result.kind == TimeKind::Date, "parse at offset");
    expect(result.date.year == 2026, "parse at offset year");
    expect(result.offset == 5, "offset is 5");
}

void test_parse_at_end() {
    const std::string_view text = "abc";
    const auto result = parse_time_at(text, 3);
    expect(result.kind == TimeKind::Invalid, "parse at end is Invalid");
}

// --- Edge cases ---

void test_edge_leap_year_feb_29() {
    // 2028 is a leap year (divisible by 4, not by 100).
    const auto result = parse_time("2028-02-29");
    expect(result.kind == TimeKind::Date, "2028-02-29 valid");
}

void test_edge_century_not_leap() {
    // 1900 is not a leap year (divisible by 100 but not 400).
    const auto result = parse_time("1900-02-29");
    expect(result.kind == TimeKind::Invalid, "1900-02-29 invalid");
}

void test_edge_400_year_leap() {
    // 2000 is a leap year (divisible by 400).
    const auto result = parse_time("2000-02-29");
    expect(result.kind == TimeKind::Date, "2000-02-29 valid");
}

void test_edge_june_30() {
    const auto result = parse_time("2026-06-30");
    expect(result.kind == TimeKind::Date, "June 30 valid");
}

void test_edge_july_31() {
    const auto result = parse_time("2026-07-31");
    expect(result.kind == TimeKind::Date, "July 31 valid");
}

void test_edge_february_28_non_leap() {
    const auto result = parse_time("2026-02-28");
    expect(result.kind == TimeKind::Date, "Feb 28 valid in non-leap year");
}

void test_epoch_midnight() {
    // 86400 = exactly 1 day after epoch.
    const auto result = parse_time("000000086400");
    expect(result.kind == TimeKind::Epoch, "86400 epoch");
    expect(result.date.hour == 0, "86400 is midnight");
    expect(result.date.day == 2, "86400 is the 2nd");
}

const mm::test::case_ cases[]{
    {"ISO date", &test_iso_date},
    {"ISO date leap year", &test_iso_date_leap_year},
    {"ISO date non-leap Feb 29 rejected", &test_iso_date_non_leap_feb_29_rejected},
    {"ISO date bad month", &test_iso_date_bad_month},
    {"ISO date bad day", &test_iso_date_bad_day},
    {"ISO date year 0", &test_iso_date_year_zero},
    {"ISO date year 9999", &test_iso_date_year_9999},
    {"slash date", &test_slash_date},
    {"US date", &test_us_date},
    {"US date invalid", &test_us_date_invalid},
    {"ISO datetime T", &test_iso_datetime_T},
    {"ISO datetime space", &test_iso_datetime_space},
    {"ISO datetime bad hour", &test_iso_datetime_bad_hour},
    {"time 3 fields", &test_time_three_fields},
    {"time 2 fields", &test_time_two_fields},
    {"time bad hour", &test_time_bad_hour},
    {"time bad minute", &test_time_bad_minute},
    {"epoch basic", &test_epoch_basic},
    {"epoch zero", &test_epoch_zero},
    {"epoch leap year", &test_epoch_leap_year},
    {"epoch too few digits", &test_epoch_too_few_digits},
    {"epoch overflow", &test_epoch_overflow},
    {"duration seconds", &test_duration_seconds},
    {"duration minutes", &test_duration_minutes},
    {"duration hours", &test_duration_hours},
    {"duration days", &test_duration_days},
    {"duration weeks", &test_duration_weeks},
    {"invalid empty", &test_invalid_empty},
    {"invalid whitespace", &test_invalid_whitespace},
    {"invalid mixed separators", &test_invalid_mixed_separators},
    {"invalid no year", &test_invalid_no_year},
    {"parse at offset", &test_parse_at_offset},
    {"parse at end", &test_parse_at_end},
    {"edge leap year Feb 29", &test_edge_leap_year_feb_29},
    {"edge century not leap", &test_edge_century_not_leap},
    {"edge 400-year leap", &test_edge_400_year_leap},
    {"edge June 30", &test_edge_june_30},
    {"edge July 31", &test_edge_july_31},
    {"edge Feb 28 non-leap", &test_edge_february_28_non_leap},
    {"epoch midnight", &test_epoch_midnight},
};

const mm::test::registrar reg{"mm.parse time", cases};

}  // namespace
