// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdint>
#include <ctime>
#include <linux/rtc.h>
#include <time.h>

import mm.test;

namespace {

using mm::test::expect;

struct DateTime {
    unsigned int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int weekday = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
};

bool leap(unsigned int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool valid(const DateTime& value) {
    static constexpr unsigned int days[]{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (value.year < 1900 || value.month < 1 || value.month > 12 ||
        value.hour > 23 || value.minute > 59 || value.second > 59)
        return false;
    auto limit = days[value.month - 1];
    if (value.month == 2 && leap(value.year)) ++limit;
    return value.day >= 1 && value.day <= limit;
}

bool convert_utc(const DateTime& value, time_t& epoch) {
    if (!valid(value)) return false;
    std::tm fields{};
    fields.tm_year = static_cast<int>(value.year) - 1900;
    fields.tm_mon = static_cast<int>(value.month) - 1;
    fields.tm_mday = value.day;
    fields.tm_hour = value.hour;
    fields.tm_min = value.minute;
    fields.tm_sec = value.second;
    fields.tm_isdst = 0;
    epoch = ::timegm(&fields);
    return epoch != static_cast<time_t>(-1);
}

bool convert_local(const DateTime& value, time_t& epoch) {
    if (!valid(value)) return false;
    std::tm fields{};
    fields.tm_year = static_cast<int>(value.year) - 1900;
    fields.tm_mon = static_cast<int>(value.month) - 1;
    fields.tm_mday = value.day;
    fields.tm_hour = value.hour;
    fields.tm_min = value.minute;
    fields.tm_sec = value.second;

    static constexpr int isdsts[]{1, 0, -1};
    for (const int isdst : isdsts) {
        std::tm try_fields = fields;
        try_fields.tm_isdst = isdst;
        const auto candidate_epoch = ::mktime(&try_fields);
        if (candidate_epoch == static_cast<time_t>(-1)) continue;
        std::tm check{};
        ::localtime_r(&candidate_epoch, &check);
        if (check.tm_year == fields.tm_year && check.tm_mon == fields.tm_mon &&
            check.tm_mday == fields.tm_mday && check.tm_hour == fields.tm_hour &&
            check.tm_min == fields.tm_min && check.tm_sec == fields.tm_sec) {
            epoch = candidate_epoch;
            return true;
        }
    }
    return false;
}

void test_rtc_date_validation_and_conversions() {
    // Leap year assertions
    expect(valid({2024, 2, 29, 0, 12, 0, 0}), "2024-02-29 is valid leap day");
    expect(!valid({2023, 2, 29, 0, 12, 0, 0}), "2023-02-29 is invalid non-leap day");

    // Invalid fields
    expect(!valid({2024, 13, 1, 0, 0, 0, 0}), "month 13 is invalid");
    expect(!valid({2024, 1, 1, 0, 24, 0, 0}), "hour 24 is invalid");
    expect(!valid({2024, 1, 1, 0, 0, 60, 0}), "minute 60 is invalid");
    expect(!valid({2024, 1, 1, 0, 0, 0, 60}), "second 60 is invalid");

    // Valid UTC conversion
    time_t epoch = 0;
    expect(convert_utc({2026, 9, 15, 0, 12, 0, 0}, epoch), "UTC conversion succeeds");
    expect(epoch > 0, "epoch is non-zero positive integer");

    // Local conversion
    expect(convert_local({2026, 9, 15, 0, 12, 0, 0}, epoch),
           "regular local conversion succeeds");
}

enum class RtcWriteResult { Ok, TransportError };

RtcWriteResult mock_rtc_write(bool clr_supported, bool clear_succeeds,
                             bool& wrote, bool& clear_supported) {
    if (!clr_supported) {
        clear_supported = false;
        wrote = true;
        return RtcWriteResult::Ok;
    }
    clear_supported = true;
    if (!clear_succeeds) {
        // DATA_INVALID remained set despite supported RTC_VL_CLR
        return RtcWriteResult::TransportError;
    }
    wrote = true;
    return RtcWriteResult::Ok;
}

bool mock_rtc_read_trusted(bool has_voltage, unsigned int voltage,
                          bool wrote, bool clear_supported) {
    const bool invalid = has_voltage && ((voltage & RTC_VL_DATA_INVALID) != 0);
    return !invalid || (wrote && !clear_supported);
}

void test_rtc_latch_and_trust_contracts() {
    bool wrote = false;
    bool clear_supported = true;

    // Case 1: Device with working RTC_VL_CLR where clear succeeds
    auto res = mock_rtc_write(true, true, wrote, clear_supported);
    expect(res == RtcWriteResult::Ok, "write with working RTC_VL_CLR succeeds");
    expect(mock_rtc_read_trusted(true, 0, wrote, clear_supported),
           "later read with DATA_INVALID clear reports trusted");

    // Case 2: Device with working RTC_VL_CLR where part refuses to clear
    wrote = false;
    res = mock_rtc_write(true, false, wrote, clear_supported);
    expect(res == RtcWriteResult::TransportError,
           "write on part that refuses to clear DATA_INVALID reports TransportError");

    // Case 3: Device whose RTC_VL_CLR is unsupported (EINVAL or ENOTTY)
    wrote = false;
    res = mock_rtc_write(false, false, wrote, clear_supported);
    expect(res == RtcWriteResult::Ok,
           "write on device without RTC_VL_CLR succeeds and remembers write");
    expect(mock_rtc_read_trusted(true, RTC_VL_DATA_INVALID, wrote, clear_supported),
           "remembered write on device without RTC_VL_CLR yields trusted");

    // Case 4: Reading from clock before any write on un-cleared device
    wrote = false;
    expect(!mock_rtc_read_trusted(true, RTC_VL_DATA_INVALID, wrote, clear_supported),
           "stale invalid bit without write reports untrusted");
}

const mm::test::case_ cases[]{
    {"RTC date validation and conversions",
     &test_rtc_date_validation_and_conversions},
    {"RTC latch and trust contracts",
     &test_rtc_latch_and_trust_contracts},
};
const mm::test::registrar reg{"platform.linux.rtc", cases};

}
