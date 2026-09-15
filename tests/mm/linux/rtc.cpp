// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdlib>
#include <ctime>
#include <linux/rtc.h>
#include <string>

import mm.rtc;
import mm.test;
import platform.linux.map;
import platform.linux.rtc;

namespace {

using mm::test::expect;

void date_validation_and_transitions() {
    using platform::linux::RtcConvention;
    using platform::linux::rtc_testing::convert;
    using platform::linux::rtc_testing::valid;

    expect(valid({2024, 2, 29, 0, 12, 0, 0}),
           "2024-02-29 is valid");
    expect(!valid({2023, 2, 29, 0, 12, 0, 0}),
           "2023-02-29 is invalid");

    long long epoch = 0;
    expect(convert({2026, 9, 15, 0, 12, 0, 0}, RtcConvention::Utc,
                   epoch) && epoch > 0,
           "UTC conversion succeeds");

    const char* previous = std::getenv("TZ");
    const std::string saved = previous ? previous : "";
    const bool had_previous = previous != nullptr;
    ::setenv("TZ", "America/Chicago", 1);
    ::tzset();

    expect(!convert({2026, 3, 8, 0, 2, 30, 0}, RtcConvention::Local,
                    epoch),
           "a skipped local hour is rejected");
    expect(convert({2026, 11, 1, 0, 1, 30, 0}, RtcConvention::Local,
                   epoch),
           "an ambiguous local hour is accepted");

    std::tm utc{};
    utc.tm_year = 2026 - 1900;
    utc.tm_mon = 10;
    utc.tm_mday = 1;
    utc.tm_hour = 6;
    utc.tm_min = 30;
    expect(epoch == static_cast<long long>(::timegm(&utc)),
           "the earlier occurrence of an ambiguous hour is selected");

    if (had_previous) ::setenv("TZ", saved.c_str(), 1);
    else ::unsetenv("TZ");
    ::tzset();
}

void voltage_latch_trust() {
    using platform::linux::rtc_testing::trusted;
    expect(trusted(true, 0, false, true),
           "a clear DATA_INVALID bit is trusted");
    expect(!trusted(true, RTC_VL_DATA_INVALID, false, true),
           "an uncleared DATA_INVALID bit is untrusted");
    expect(trusted(false, 0, true, false),
           "a remembered write is trusted when clearing is unsupported");
    expect(!trusted(false, 0, false, true),
           "lack of voltage-latch support does not invent trust");
}

const mm::test::case_ cases[]{
    {"RTC date validation and DST transitions", &date_validation_and_transitions},
    {"RTC voltage-latch trust", &voltage_latch_trust},
};
const mm::test::registrar reg{"platform.linux.rtc", cases};

}
