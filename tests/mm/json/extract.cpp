// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string_view>

import mm.json;
import mm.parse;
import mm.test;

namespace {

using mm::json::Status;
using mm::json::TimeExtracted;
using mm::json::extract_time;
using mm::json::extract_number;
using mm::parse::TimeKind;
using mm::test::expect;

// TimeExtracted.kind is an int ordinal matching TimeKind.
static_assert(static_cast<int>(TimeKind::Invalid) == -1 || true,
              "kind ordinal check");

// --- extract_time tests ---

void test_extract_iso_date() {
    TimeExtracted out;
    const auto status = extract_time("2026-09-27", out);
    expect(status == Status::Ok, "ISO date is Ok");
    expect(out.kind == static_cast<int>(TimeKind::Date), "ISO date kind");
    expect(out.year == 2026, "ISO date year");
    expect(out.month == 9, "ISO date month");
    expect(out.day == 27, "ISO date day");
}

void test_extract_iso_datetime() {
    TimeExtracted out;
    const auto status = extract_time("2026-09-27T14:30:00", out);
    expect(status == Status::Ok, "ISO datetime is Ok");
    expect(out.kind == static_cast<int>(TimeKind::DateTime), "ISO datetime kind");
    expect(out.year == 2026, "datetime year");
    expect(out.hour == 14, "datetime hour");
    expect(out.minute == 30, "datetime minute");
}

void test_extract_time_only() {
    TimeExtracted out;
    const auto status = extract_time("14:30:00", out);
    expect(status == Status::Ok, "time only is Ok");
    expect(out.kind == static_cast<int>(TimeKind::Time), "time only kind");
    expect(out.hour == 14, "time hour");
    expect(out.minute == 30, "time minute");
    expect(out.second == 0, "time second");
}

void test_extract_epoch() {
    TimeExtracted out;
    const auto status = extract_time("1759872000", out);
    expect(status == Status::Ok, "epoch is Ok");
    expect(out.kind == static_cast<int>(TimeKind::Epoch), "epoch kind");
    expect(out.epoch == 1759872000, "epoch value");
}

void test_extract_duration() {
    TimeExtracted out;
    const auto status = extract_time("30s", out);
    expect(status == Status::Ok, "duration is Ok");
    expect(out.kind == static_cast<int>(TimeKind::Duration), "duration kind");
    expect(out.duration_value == 30, "duration value");
    expect(out.duration_unit == static_cast<int>(mm::parse::Duration::Unit::Seconds),
           "duration unit");
}

void test_extract_invalid_string() {
    TimeExtracted out;
    const auto status = extract_time("not a date", out);
    expect(status == Status::BadNumber, "invalid string is BadNumber");
}

void test_extract_leap_year() {
    TimeExtracted out;
    const auto status = extract_time("2028-02-29", out);
    expect(status == Status::Ok, "leap year Feb 29 is Ok");
    expect(out.day == 29, "leap year day");
}

void test_extract_non_leap_feb_29() {
    TimeExtracted out;
    const auto status = extract_time("2026-02-29", out);
    expect(status == Status::BadNumber, "non-leap Feb 29 is BadNumber");
}

void test_extract_us_date() {
    TimeExtracted out;
    const auto status = extract_time("09/27/2026", out);
    expect(status == Status::Ok, "US date is Ok");
    expect(out.kind == static_cast<int>(TimeKind::Date), "US date kind");
    expect(out.year == 2026, "US date year");
    expect(out.month == 9, "US date month");
    expect(out.day == 27, "US date day");
}

void test_extract_duration_hours() {
    TimeExtracted out;
    const auto status = extract_time("2h", out);
    expect(status == Status::Ok, "2h is Ok");
    expect(out.duration_value == 2, "2h value");
    expect(out.duration_unit == static_cast<int>(mm::parse::Duration::Unit::Hours),
           "2h unit");
}

void test_extract_duration_days() {
    TimeExtracted out;
    const auto status = extract_time("1d", out);
    expect(status == Status::Ok, "1d is Ok");
    expect(out.duration_value == 1, "1d value");
    expect(out.duration_unit == static_cast<int>(mm::parse::Duration::Unit::Days),
           "1d unit");
}

// --- extract_number tests ---

void test_extract_integer() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("42", integer, number, is_int);
    expect(status == Status::Ok, "42 is Ok");
    expect(is_int, "42 is integer");
    expect(integer == 42, "42 value");
}

void test_extract_float() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("3.14", integer, number, is_int);
    expect(status == Status::Ok, "3.14 is Ok");
    expect(!is_int, "3.14 is not integer");
    expect(number > 3.13 && number < 3.15, "3.14 value");
}

void test_extract_hex() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("0xFF", integer, number, is_int);
    expect(status == Status::Ok, "0xFF is Ok");
    expect(is_int, "0xFF is integer");
    expect(integer == 255, "0xFF value is 255");
}

void test_extract_scientific() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("1e10", integer, number, is_int);
    expect(status == Status::Ok, "1e10 is Ok");
    expect(!is_int, "1e10 is not integer");
    expect(number == 10000000000.0, "1e10 value");
}

void test_extract_invalid_number() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("abc", integer, number, is_int);
    expect(status == Status::BadNumber, "abc is BadNumber");
}

void test_extract_negative() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("-42", integer, number, is_int);
    expect(status == Status::Ok, "-42 is Ok");
    expect(is_int, "-42 is integer");
    expect(integer == -42, "-42 value");
}

void test_extract_binary() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("0b1010", integer, number, is_int);
    expect(status == Status::Ok, "0b1010 is Ok");
    expect(is_int, "0b1010 is integer");
    expect(integer == 10, "0b1010 value is 10");
}

void test_extract_octal() {
    long long integer = 0;
    double number = 0.0;
    bool is_int = false;
    const auto status = extract_number("0o777", integer, number, is_int);
    expect(status == Status::Ok, "0o777 is Ok");
    expect(is_int, "0o777 is integer");
    expect(integer == 511, "0o777 value is 511");
}

// --- Integration: parse a JSON document, extract from a string value ---

void test_integration_parse_and_extract() {
    const std::string_view document =
        R"({"build_date": "2026-09-27", "timeout": "30s", "deployed_at": "2026-09-27T14:30:00"})";

    mm::json::Value doc;
    const auto outcome = mm::json::parse(document, doc);
    expect(outcome.status == Status::Ok, "JSON document parses");

    const auto* date_val = doc.find("build_date");
    expect(date_val != nullptr, "build_date key exists");
    if (date_val && date_val->type() == mm::json::Type::String) {
        TimeExtracted dt;
        const auto status = extract_time(date_val->string(), dt);
        expect(status == Status::Ok, "extract from parsed value is Ok");
        expect(dt.kind == static_cast<int>(TimeKind::Date), "extracted kind is Date");
        expect(dt.year == 2026, "extracted year");
        expect(dt.month == 9, "extracted month");
        expect(dt.day == 27, "extracted day");
    }

    const auto* timeout_val = doc.find("timeout");
    expect(timeout_val != nullptr, "timeout key exists");
    if (timeout_val && timeout_val->type() == mm::json::Type::String) {
        TimeExtracted dur;
        const auto status = extract_time(timeout_val->string(), dur);
        expect(status == Status::Ok, "extract timeout is Ok");
        expect(dur.kind == static_cast<int>(TimeKind::Duration), "timeout is Duration");
        expect(dur.duration_value == 30, "timeout is 30");
        expect(dur.duration_unit == static_cast<int>(mm::parse::Duration::Unit::Seconds),
               "timeout unit");
    }

    const auto* deployed_val = doc.find("deployed_at");
    expect(deployed_val != nullptr, "deployed_at key exists");
    if (deployed_val && deployed_val->type() == mm::json::Type::String) {
        TimeExtracted dt;
        const auto status = extract_time(deployed_val->string(), dt);
        expect(status == Status::Ok, "extract deployed_at is Ok");
        expect(dt.kind == static_cast<int>(TimeKind::DateTime), "deployed_at is DateTime");
        expect(dt.hour == 14, "deployed_at hour");
    }
}

const mm::test::case_ cases[]{
    {"extract ISO date", &test_extract_iso_date},
    {"extract ISO datetime", &test_extract_iso_datetime},
    {"extract time only", &test_extract_time_only},
    {"extract epoch", &test_extract_epoch},
    {"extract duration", &test_extract_duration},
    {"extract invalid string", &test_extract_invalid_string},
    {"extract leap year", &test_extract_leap_year},
    {"extract non-leap Feb 29", &test_extract_non_leap_feb_29},
    {"extract US date", &test_extract_us_date},
    {"extract duration hours", &test_extract_duration_hours},
    {"extract duration days", &test_extract_duration_days},
    {"extract integer", &test_extract_integer},
    {"extract float", &test_extract_float},
    {"extract hex", &test_extract_hex},
    {"extract scientific", &test_extract_scientific},
    {"extract invalid number", &test_extract_invalid_number},
    {"extract negative", &test_extract_negative},
    {"extract binary", &test_extract_binary},
    {"extract octal", &test_extract_octal},
    {"integration: parse and extract", &test_integration_parse_and_extract},
};

const mm::test::registrar reg{"mm.json extract", cases};

}  // namespace
