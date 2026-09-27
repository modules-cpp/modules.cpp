// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <string_view>

module mm.parse;

import :time;

namespace mm::parse {
namespace {

constexpr auto epoch_overflow = std::uint64_t(1e18);

[[nodiscard]] bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Howard Hinnant's days-from-civil: maps (year, month, day) to days
// since 1970-01-01. Pure integer arithmetic, no allocation.
[[nodiscard]] std::int64_t days_from_civil(std::int64_t y,
                                             std::int64_t m,
                                             std::int64_t d) {
    const auto era = (m > 2) ? y : y - 1;
    const auto yoe = era - (m > 2 ? 1 : 0);  // year of era
    const auto doy =
        ((3 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;  // day of year
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doe;
    return era * 365 + (era - 1) / 4 - (era - 1) / 100 + (era - 1) / 400 - 719468 + doe;
}

// The inverse: days since 1970-01-01 to (year, month, day).
void civil_from_days(std::int64_t days,
                      unsigned int& y, unsigned int& m, unsigned int& d) {
    const auto z = days + 719468;
    const auto era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = z - era * 146097;
    const auto yoe = (doe - doe / 100 + doe / 400 - doe / 2000) / 365;
    const auto y = yoe + era * 100;
    const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100 + yoe / 2000);
    const auto mp = (5 * doy + 2) / 153;
    const auto d_ = doy - (153 * mp - 2) / 5 + 1;
    const auto m_ = (mp < 10) ? mp + 3 : mp - 9;
    y = static_cast<unsigned int>((m_ <= 2) ? y + 1 : y);
    m = static_cast<unsigned int>(m_);
    d = static_cast<unsigned int>(d_);
}

// Leap year check.
[[nodiscard]] bool is_leap(unsigned int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

// Max days in month m (1-based) for year y.
[[nodiscard]] unsigned int month_days(unsigned int y, unsigned int m) {
    switch (m) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return is_leap(y) ? 29 : 28;
        default:
            return 0;
    }
}

// Parse exactly `count` digits starting at `pos`, advancing `pos`.
// Returns the accumulated value or -1 on failure.
[[nodiscard]] std::int64_t read_digits(std::string_view text,
                                         std::size_t& pos,
                                         std::size_t count) {
    if (pos + count > text.size()) return -1;
    std::int64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!is_digit(text[pos + i])) return -1;
        value = value * 10 + (text[pos + i] - '0');
    }
    pos += count;
    return value;
}

// Parse a variable-length digit run starting at `pos`.
[[nodiscard]] std::int64_t read_digit_run(std::string_view text,
                                            std::size_t& pos,
                                            std::size_t max_digits) {
    std::int64_t value = 0;
    std::size_t count = 0;
    while (pos < text.size() && is_digit(text[pos]) && count < max_digits) {
        value = value * 10 + (text[pos] - '0');
        ++pos;
        ++count;
    }
    return count > 0 ? value : -1;
}

}  // namespace

[[nodiscard]] TimeValue parse_time(std::string_view text,
                                     std::size_t at) {
    TimeValue result;
    result.offset = at;

    if (at >= text.size()) return result;

    std::size_t pos = at;

    // Skip leading whitespace.
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' ||
            text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }

    if (pos >= text.size()) return result;

    // --- Duration: digits immediately followed by s/m/h/d/w ---
    // Check: if the text is all digits + one trailing letter, it's a duration.
    {
        std::size_t digit_end = pos;
        while (digit_end < text.size() && is_digit(text[digit_end])) ++digit_end;
        if (digit_end > pos && digit_end < text.size()) {
            const char unit = text[digit_end];
            if (unit == 's' || unit == 'm' || unit == 'h' ||
                unit == 'd' || unit == 'w') {
                // Confirm nothing follows the unit letter.
                if (digit_end + 1 >= text.size()) {
                    std::int64_t value = 0;
                    for (std::size_t i = pos; i < digit_end; ++i) {
                        value = value * 10 + (text[i] - '0');
                    }
                    result.kind = TimeKind::Duration;
                    result.length = digit_end - at + 1;
                    result.consumed = result.length;
                    result.duration.value = static_cast<std::uint64_t>(value);
                    switch (unit) {
                        case 's': result.duration.unit = Duration::Unit::Seconds; break;
                        case 'm': result.duration.unit = Duration::Unit::Minutes; break;
                        case 'h': result.duration.unit = Duration::Unit::Hours; break;
                        case 'd': result.duration.unit = Duration::Unit::Days; break;
                        case 'w': result.duration.unit = Duration::Unit::Weeks; break;
                    }
                    return result;
                }
            }
        }
    }

    // --- Epoch: 10-12 digit decimal integer, no separators ---
    {
        std::size_t digit_end = pos;
        while (digit_end < text.size() && is_digit(text[digit_end])) ++digit_end;
        const std::size_t digit_count = digit_end - pos;
        if (digit_count >= 10 && digit_count <= 12) {
            // Confirm nothing follows.
            if (digit_end >= text.size() ||
                text[digit_end] == ' ' || text[digit_end] == '\t' ||
                text[digit_end] == '\n' || text[digit_end] == '\r') {
                std::uint64_t value = 0;
                for (std::size_t i = pos; i < digit_end; ++i) {
                    value = value * 10 +
                            static_cast<std::uint64_t>(text[i] - '0');
                }
                result.kind = TimeKind::Epoch;
                result.length = digit_end - at;
                result.consumed = result.length;
                result.epoch = value;
                if (value < epoch_overflow) {
                    // Convert to DateTime.
                    const auto days = static_cast<std::int64_t>(
                        value / 86400);
                    const auto rem = value % 86400;
                    civil_from_days(days,
                                      result.date.year,
                                      result.date.month,
                                      result.date.day);
                    result.date.hour = static_cast<unsigned int>(rem / 3600);
                    result.date.minute =
                        static_cast<unsigned int>((rem % 3600) / 60);
                    result.date.second =
                        static_cast<unsigned int>(rem % 60);
                } else {
                    result.overflow = true;
                }
                return result;
            }
        }
    }

    // --- Time: HH:MM:SS or HH:MM ---
    {
        std::size_t tpos = pos;
        std::int64_t hour = read_digit_run(text, tpos, 2);
        if (hour >= 0 && tpos < text.size() && text[tpos] == ':') {
            ++tpos;
            std::int64_t minute = read_digit_run(text, tpos, 2);
            if (minute >= 0) {
                bool has_second = false;
                std::int64_t second = 0;
                if (tpos < text.size() && text[tpos] == ':') {
                    ++tpos;
                    second = read_digit_run(text, tpos, 2);
                    if (second < 0) return result;
                    has_second = true;
                }
                if (hour >= 0 && hour <= 23 &&
                    minute >= 0 && minute <= 59 &&
                    second >= 0 && second <= 59) {
                    result.kind = TimeKind::Time;
                    result.length = tpos - at;
                    result.consumed = result.length;
                    result.date.hour = static_cast<unsigned int>(hour);
                    result.date.minute = static_cast<unsigned int>(minute);
                    result.date.second = static_cast<unsigned int>(second);
                    return result;
                }
            }
        }
    }

    // --- Date: YYYY-MM-DD, YYYY/MM/DD, MM/DD/YYYY ---
    {
        std::size_t dpos = pos;
        std::int64_t year = 0, month = 0, day = 0;
        bool valid = false;

        if (dpos + 4 <= text.size() && is_digit(text[dpos]) &&
            is_digit(text[dpos + 1]) && is_digit(text[dpos + 2]) &&
            is_digit(text[dpos + 3])) {
            // Try YYYY-MM-DD or YYYY/MM/DD.
            year = read_digits(text, dpos, 4);
            if (year >= 0 && dpos < text.size()) {
                const char sep = text[dpos];
                if (sep == '-' || sep == '/') {
                    ++dpos;
                    month = read_digit_run(text, dpos, 2);
                    if (month >= 0 && dpos < text.size() &&
                        (text[dpos] == sep)) {
                        ++dpos;
                        day = read_digit_run(text, dpos, 2);
                        if (day >= 0) {
                            if (year >= 0 && year <= 9999 &&
                                month >= 1 && month <= 12 &&
                                day >= 1 && day <= month_days(
                                    static_cast<unsigned int>(year),
                                    static_cast<unsigned int>(month))) {
                                valid = true;
                            }
                        }
                    }
                }
            }
            if (valid) {
                result.kind = TimeKind::Date;
                result.length = dpos - at;
                result.consumed = result.length;
                result.date.year = static_cast<unsigned int>(year);
                result.date.month = static_cast<unsigned int>(month);
                result.date.day = static_cast<unsigned int>(day);

                // Check for DateTime: [T or space] HH:MM:SS
                std::size_t tpos = dpos;
                if (tpos < text.size() &&
                    (text[tpos] == 'T' || text[tpos] == ' ')) {
                    std::size_t hpos = tpos + 1;
                    std::int64_t hour = read_digit_run(text, hpos, 2);
                    if (hour >= 0 && hpos < text.size() &&
                        text[hpos] == ':') {
                        ++hpos;
                        std::int64_t minute = read_digit_run(text, hpos, 2);
                        if (minute >= 0) {
                            std::int64_t second = 0;
                            bool has_sec = false;
                            if (hpos < text.size() && text[hpos] == ':') {
                                ++hpos;
                                second = read_digit_run(text, hpos, 2);
                                if (second >= 0) has_sec = true;
                            }
                            if (hour >= 0 && hour <= 23 &&
                                minute >= 0 && minute <= 59 &&
                                second >= 0 && second <= 59) {
                                result.kind = TimeKind::DateTime;
                                result.length = hpos - at;
                                result.consumed = result.length;
                                result.date.hour =
                                    static_cast<unsigned int>(hour);
                                result.date.minute =
                                    static_cast<unsigned int>(minute);
                                result.date.second =
                                    static_cast<unsigned int>(second);
                                return result;
                            }
                        }
                    }
                }
                return result;
            }
        }

        // Try MM/DD/YYYY (US format).
        if (dpos + 2 <= text.size() && is_digit(text[dpos]) &&
            is_digit(text[dpos + 1])) {
            std::size_t tmp = dpos;
            std::int64_t m1 = read_digit_run(text, tmp, 2);
            if (m1 >= 0 && tmp < text.size() && text[tmp] == '/') {
                ++tmp;
                std::int64_t d1 = read_digit_run(text, tmp, 2);
                if (d1 >= 0 && tmp < text.size() && text[tmp] == '/') {
                    ++tmp;
                    std::int64_t y1 = read_digit_run(text, tmp, 4);
                    if (y1 >= 0 && y1 <= 9999 &&
                        m1 >= 1 && m1 <= 12 &&
                        d1 >= 1 && d1 <= month_days(
                            static_cast<unsigned int>(y1),
                            static_cast<unsigned int>(m1))) {
                        result.kind = TimeKind::Date;
                        result.length = tmp - at;
                        result.consumed = result.length;
                        result.date.year =
                            static_cast<unsigned int>(y1);
                        result.date.month =
                            static_cast<unsigned int>(m1);
                        result.date.day =
                            static_cast<unsigned int>(d1);
                    }
                }
            }
        }
    }

    return result;
}

[[nodiscard]] TimeValue parse_time(std::string_view text) {
    return parse_time(text, 0);
}

}  // namespace mm::parse
