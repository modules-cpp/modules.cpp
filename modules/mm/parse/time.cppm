// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module mm.parse:time;

export namespace mm::parse {

// A calendar instant. Mirrors mm.rtc::DateTime but owned by mm.parse so
// that modules that do not import mm.rtc can still parse dates.
struct DateTime {
    unsigned int year = 0;
    unsigned int month = 1;
    unsigned int day = 1;
    unsigned int weekday = 0;  // 0 = unspecified
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
};

// A duration in a single unit.
struct Duration {
    std::uint64_t value = 0;
    enum class Unit { Seconds, Minutes, Hours, Days, Weeks } unit = Unit::Seconds;
};

// The kind of time value a token holds.
enum class TimeKind {
    Invalid,
    Date,       // YYYY-MM-DD, YYYY/MM/DD, MM/DD/YYYY
    DateTime,   // YYYY-MM-DD[ T]HH:MM:SS
    Time,       // HH:MM:SS or HH:MM
    Epoch,      // decimal seconds since 1970
    Duration,   // Ns, Nm, Nh, Nd, Nw
};

// The result of parsing one time token.
struct TimeValue {
    TimeKind kind = TimeKind::Invalid;
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t consumed = 0;
    DateTime date;
    Duration duration;
    std::uint64_t epoch = 0;
    bool overflow = false;
};

// Parse a time value starting at `at` in `text`.
[[nodiscard]] TimeValue parse_time(std::string_view text,
                                     std::size_t at = 0);

// Parse the entire text as a single time value.
[[nodiscard]] TimeValue parse_time(std::string_view text);

}  // namespace mm::parse
