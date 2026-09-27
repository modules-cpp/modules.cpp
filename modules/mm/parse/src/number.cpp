// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cctype>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

module mm.parse;

import :number;

namespace mm::parse {
namespace {

constexpr auto int64_max = std::numeric_limits<std::int64_t>::max();
constexpr auto int64_min = std::numeric_limits<std::int64_t>::min();

[[nodiscard]] bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Strip underscore separators from a digit sequence, returning the cleaned
// length. Returns false if underscores are malformed (leading, trailing,
// doubled).
[[nodiscard]] bool strip_underscores(std::string_view digits,
                                      std::size_t& clean_length) {
    clean_length = 0;
    bool prev_underscore = false;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        const char c = digits[i];
        if (c == '_') {
            if (i == 0 || prev_underscore || i == digits.size() - 1) {
                return false;
            }
            prev_underscores = true;
            continue;
        }
        if (!is_digit(c)) return false;
        ++clean_length;
        prev_underscore = false;
    }
    return clean_length > 0;
}

}  // namespace

[[nodiscard]] NumberValue parse_number(std::string_view text,
                                        std::size_t at) {
    NumberValue result;
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

    // Optional sign.
    bool negative = false;
    if (text[pos] == '-' || text[pos] == '+') {
        negative = (text[pos] == '-');
        ++pos;
    }

    if (pos >= text.size()) return result;

    // Hex, octal, binary prefixes.
    bool is_hex = false, is_octal = false, is_binary = false;
    if (text[pos] == '0' && pos + 1 < text.size()) {
        const char next = text[pos + 1];
        if (next == 'x' || next == 'X') {
            is_hex = true;
            pos += 2;
        } else if (next == 'o' || next == 'O') {
            is_octal = true;
            pos += 2;
        } else if (next == 'b' || next == 'B') {
            is_binary = true;
            pos += 2;
        }
    }

    // Collect digits. For decimal, collect through the optional fractional
    // and exponent parts.
    bool has_fraction = false;
    bool has_exponent = false;
    std::size_t number_end = pos;

    if (is_hex || is_octal || is_binary) {
        // Integer-only radices: no fraction, no exponent.
        while (number_end < text.size()) {
            const char c = text[number_end];
            if (c == '_') {
                // Allow underscores in prefixed radices too.
                number_end++;
                continue;
            }
            if (is_hex) {
                if (!is_hex_digit(c)) break;
            } else if (is_octal) {
                if (c < '0' || c > '7') break;
            } else {
                if (c != '0' && c != '1') break;
            }
            ++number_end;
        }
    } else {
        // Decimal: digits, optional .fraction, optional e/E exponent.
        // Skip underscores between digits.
        std::size_t i = pos;
        bool any_digit = false;

        // Integer part.
        while (i < text.size()) {
            const char c = text[i];
            if (c == '_') { ++i; continue; }
            if (!is_digit(c)) break;
            any_digit = true;
            ++i;
        }
        if (!any_digit) return result;  // No digits at all.

        number_end = i;

        // Fractional part.
        if (number_end < text.size() && text[number_end] == '.') {
            has_fraction = true;
            ++number_end;
            bool frac_digit = false;
            while (number_end < text.size()) {
                const char c = text[number_end];
                if (c == '_') { ++number_end; continue; }
                if (!is_digit(c)) break;
                frac_digit = true;
                ++number_end;
            }
            // Trailing dot with no fractional digits is allowed: "1."
        }

        // Exponent part.
        if (number_end < text.size() &&
            (text[number_end] == 'e' || text[number_end] == 'E')) {
            has_exponent = true;
            ++number_end;
            if (number_end < text.size() &&
                (text[number_end] == '+' || text[number_end] == '-')) {
                ++number_end;
            }
            bool exp_digit = false;
            while (number_end < text.size()) {
                const char c = text[number_end];
                if (c == '_') { ++number_end; continue; }
                if (!is_digit(c)) break;
                exp_digit = true;
                ++number_end;
            }
            if (!exp_digit) return result;  // Exponent without digits.
        }
    }

    result.length = number_end - at;
    result.consumed = number_end - at;

    // Extract the digit substring (without sign, without prefix,
    // without underscores).
    std::size_t digits_start = pos;
    // pos may have been advanced past the prefix for hex/octal/binary.
    // For decimal, pos is at the first digit or dot.
    std::string_view digit_span = text.substr(digits_start,
                                                number_end - digits_start);

    // Remove underscores for from_chars.
    std::string cleaned;
    cleaned.reserve(digit_span.size());
    for (const char c : digit_span) {
        if (c != '_') cleaned += c;
    }

    if (is_hex) {
        std::int64_t value = 0;
        const auto* first = cleaned.data();
        const auto* last = cleaned.data() + cleaned.size();
        const auto res = std::from_chars(first, last, value, 16);
        if (res.ec != std::errc{} || res.ptr != last) {
            return result;
        }
        if (negative && value > 0) {
            result.integer = -value;
        } else {
            result.integer = value;
        }
        result.kind = NumberKind::Integer;
        return result;
    }

    if (is_octal) {
        std::int64_t value = 0;
        const auto* first = cleaned.data();
        const auto* last = cleaned.data() + cleaned.size();
        const auto res = std::from_chars(first, last, value, 8);
        if (res.ec != std::errc{} || res.ptr != last) {
            return result;
        }
        result.integer = negative ? -value : value;
        result.kind = NumberKind::Integer;
        return result;
    }

    if (is_binary) {
        std::int64_t value = 0;
        const auto* first = cleaned.data();
        const auto* last = cleaned.data() + cleaned.size();
        const auto res = std::from_chars(first, last, value, 2);
        if (res.ec != std::errc{} || res.ptr != last) {
            return result;
        }
        result.integer = negative ? -value : value;
        result.kind = NumberKind::Integer;
        return result;
    }

    // Decimal integer (no fraction, no exponent).
    if (!has_fraction && !has_exponent) {
        // The cleaned string may start with '-' or '+' if the sign was
        // included in the digit_span. But we stripped the sign above, so
        // cleaned is digits only.
        std::int64_t value = 0;
        const auto* first = cleaned.data();
        const auto* last = cleaned.data() + cleaned.size();
        const auto res = std::from_chars(first, last, value);
        if (res.ec == std::errc::result_out_of_range) {
            result.overflow = true;
            result.integer = negative ? int64_min : int64_max;
            result.kind = NumberKind::Integer;
            return result;
        }
        if (res.ec != std::errc{} || res.ptr != last) {
            return result;
        }
        result.integer = negative ? -value : value;
        result.kind = NumberKind::Integer;
        return result;
    }

    // Float or Scientific: use strtod (from_chars for double is not
    // universally available; see mm.json's note).
    std::string input;
    input.reserve(cleaned.size() + 1);
    if (negative) input += '-';
    input += cleaned;

    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(input.c_str(), &end);
    if (end != input.c_str() + input.size()) {
        return result;  // Not all of the input was consumed.
    }
    if (errno == ERANGE) {
        result.overflow = true;
        // Underflow: clamp to 0.0 or -0.0. Overflow: clamp to inf.
        if (std::fabs(parsed) == 0.0) {
            result.real = negative ? -0.0 : 0.0;
        } else {
            result.real = negative ? -HUGE_VAL : HUGE_VAL;
        }
    } else {
        result.real = parsed;
    }
    result.kind = has_exponent ? NumberKind::Scientific
                               : NumberKind::Float;
    return result;
}

[[nodiscard]] NumberValue parse_number(std::string_view text) {
    return parse_number(text, 0);
}

}  // namespace mm::parse
