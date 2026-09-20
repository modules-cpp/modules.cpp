// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <charconv>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

module mm.json;

import :status;
import :scan;

namespace mm::json {

namespace {

[[nodiscard]] bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

[[nodiscard]] bool is_digit(char c) { return c >= '0' && c <= '9'; }

[[nodiscard]] int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] std::uint64_t bit(unsigned int level) { return std::uint64_t{1} << level; }

// The length of the well-formed UTF-8 sequence that begins at index, or zero
// when the bytes there are not one: a bad lead byte, a missing or wrong
// continuation, an overlong form, an encoded surrogate, or a code point past
// U+10FFFF, by the Unicode Standard's table of well-formed sequences.
[[nodiscard]] std::size_t utf8_sequence(std::string_view text, std::size_t index) {
    const auto byte = [&](std::size_t at) -> int {
        return at < text.size() ? static_cast<unsigned char>(text[at]) : -1;
    };
    const int lead = byte(index);
    if (lead < 0x80) return 1;
    if (lead < 0xC2) return 0;
    const int second = byte(index + 1);
    if (second < 0) return 0;
    if (lead < 0xE0) return (second & 0xC0) == 0x80 ? 2 : 0;
    const int third = byte(index + 2);
    if (lead < 0xF0) {
        const int low = lead == 0xE0 ? 0xA0 : 0x80;
        const int high = lead == 0xED ? 0x9F : 0xBF;
        if (second < low || second > high) return 0;
        return (third & 0xC0) == 0x80 ? 3 : 0;
    }
    if (lead > 0xF4) return 0;
    const int low = lead == 0xF0 ? 0x90 : 0x80;
    const int high = lead == 0xF4 ? 0x8F : 0xBF;
    if (second < low || second > high) return 0;
    const int fourth = byte(index + 3);
    if ((third & 0xC0) != 0x80) return 0;
    return (fourth & 0xC0) == 0x80 ? 4 : 0;
}

// Four hex digits at index, or -1.
[[nodiscard]] int hex_quad(std::string_view text, std::size_t index) {
    if (index + 4 > text.size()) return -1;
    int value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const int digit = hex_value(text[index + i]);
        if (digit < 0) return -1;
        value = value * 16 + digit;
    }
    return value;
}

// Why hex_quad answered -1: a character that is not a hex digit among the
// bytes present is BadEscape, and hex digits that simply run out at the end
// of the input are Truncated.
[[nodiscard]] Status why_not_hex(std::string_view text, std::size_t index) {
    for (std::size_t i = 0; i < 4 && index + i < text.size(); ++i)
        if (hex_value(text[index + i]) < 0) return Status::BadEscape;
    return Status::Truncated;
}

[[nodiscard]] bool high_surrogate(int unit) { return unit >= 0xD800 && unit <= 0xDBFF; }
[[nodiscard]] bool low_surrogate(int unit) { return unit >= 0xDC00 && unit <= 0xDFFF; }

// Encodes a code point as UTF-8 into out, when out is not empty, and answers
// the byte count either way.
[[nodiscard]] std::size_t encode(std::uint32_t point, std::span<char> out, std::size_t at) {
    const auto put = [&](std::size_t i, unsigned int value) {
        if (!out.empty()) out[at + i] = static_cast<char>(value);
    };
    if (point < 0x80) { put(0, point); return 1; }
    if (point < 0x800) {
        put(0, 0xC0 | (point >> 6));
        put(1, 0x80 | (point & 0x3F));
        return 2;
    }
    if (point < 0x10000) {
        put(0, 0xE0 | (point >> 12));
        put(1, 0x80 | ((point >> 6) & 0x3F));
        put(2, 0x80 | (point & 0x3F));
        return 3;
    }
    put(0, 0xF0 | (point >> 18));
    put(1, 0x80 | ((point >> 12) & 0x3F));
    put(2, 0x80 | ((point >> 6) & 0x3F));
    put(3, 0x80 | (point & 0x3F));
    return 4;
}

// One pass over an escaped string: validates, counts, and -- when out is
// not empty -- writes. Shared by unescape's two passes so that they cannot
// disagree.
[[nodiscard]] Decoded decode(std::string_view escaped, std::span<char> out) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < escaped.size()) {
        const char c = escaped[i];
        if (c != '\\') {
            const auto length = utf8_sequence(escaped, i);
            if (length == 0) return {Status::BadUnicode, 0};
            if (static_cast<unsigned char>(c) < 0x20) return {Status::Malformed, 0};
            if (!out.empty())
                for (std::size_t k = 0; k < length; ++k) out[count + k] = escaped[i + k];
            count += length;
            i += length;
            continue;
        }
        if (i + 1 >= escaped.size()) return {Status::BadEscape, 0};
        const char e = escaped[i + 1];
        char plain = 0;
        switch (e) {
            case '"': plain = '"'; break;
            case '\\': plain = '\\'; break;
            case '/': plain = '/'; break;
            case 'b': plain = '\b'; break;
            case 'f': plain = '\f'; break;
            case 'n': plain = '\n'; break;
            case 'r': plain = '\r'; break;
            case 't': plain = '\t'; break;
            case 'u': break;
            default: return {Status::BadEscape, 0};
        }
        if (e != 'u') {
            if (!out.empty()) out[count] = plain;
            ++count;
            i += 2;
            continue;
        }
        const int unit = hex_quad(escaped, i + 2);
        if (unit < 0) return {Status::BadEscape, 0};
        std::uint32_t point = static_cast<std::uint32_t>(unit);
        i += 6;
        if (low_surrogate(unit)) return {Status::BadUnicode, 0};
        if (high_surrogate(unit)) {
            if (i + 1 >= escaped.size() || escaped[i] != '\\' || escaped[i + 1] != 'u')
                return {Status::BadUnicode, 0};
            const int second = hex_quad(escaped, i + 2);
            if (second < 0) return {Status::BadEscape, 0};
            if (!low_surrogate(second)) return {Status::BadUnicode, 0};
            point = 0x10000 + ((point - 0xD800) << 10) + (static_cast<std::uint32_t>(second) - 0xDC00);
            i += 6;
        }
        count += encode(point, out, count);
    }
    return {Status::Ok, count};
}

}  // namespace

Scanner::Scanner(std::string_view text) : text_(text) {
    // A leading byte order mark is skipped, which RFC 8259 permits.
    if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
        static_cast<unsigned char>(text_[1]) == 0xBB &&
        static_cast<unsigned char>(text_[2]) == 0xBF)
        offset_ = 3;
}

Status Scanner::fail(Status status, std::size_t at) {
    // The line and column of the fault, walked from the cursor so that a
    // fault inside a token is placed where it is and not where the token
    // began. The cursor stays where it was.
    unsigned int line = line_;
    unsigned int column = column_;
    for (std::size_t i = offset_; i < at && i < text_.size(); ++i) {
        if (text_[i] == '\n') { ++line; column = 1; }
        else ++column;
    }
    issue_ = {at, line, column, status, describe(status)};
    return status;
}

void Scanner::advance(std::size_t count) {
    for (std::size_t i = 0; i < count && offset_ < text_.size(); ++i, ++offset_) {
        if (text_[offset_] == '\n') { ++line_; column_ = 1; }
        else ++column_;
    }
}

void Scanner::skip_whitespace() {
    while (offset_ < text_.size() && is_whitespace(text_[offset_])) advance(1);
}

Status Scanner::scan_string(std::size_t& length) {
    // offset_ is at the opening quote. Validates escapes, surrogate pairs,
    // control characters, and UTF-8 as it passes, and answers the length of
    // the bytes between the quotes on Ok.
    std::size_t i = offset_ + 1;
    while (true) {
        if (i >= text_.size()) return fail(Status::Truncated, text_.size());
        const unsigned char c = static_cast<unsigned char>(text_[i]);
        if (c == '"') break;
        if (c < 0x20) return fail(Status::Malformed, i);
        if (c == '\\') {
            if (i + 1 >= text_.size()) return fail(Status::Truncated, text_.size());
            const char e = text_[i + 1];
            if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' ||
                e == 'n' || e == 'r' || e == 't') {
                i += 2;
                continue;
            }
            if (e != 'u') return fail(Status::BadEscape, i);
            const int unit = hex_quad(text_, i + 2);
            if (unit < 0) {
                const auto why = why_not_hex(text_, i + 2);
                return fail(why, why == Status::Truncated ? text_.size() : i);
            }
            if (low_surrogate(unit)) return fail(Status::BadUnicode, i);
            i += 6;
            if (high_surrogate(unit)) {
                // The low half must follow at once; a closing quote or any
                // other text there is a lone surrogate, and only the input
                // running out mid-escape is Truncated.
                if (i >= text_.size()) return fail(Status::Truncated, text_.size());
                if (text_[i] != '\\') return fail(Status::BadUnicode, i - 6);
                if (i + 1 >= text_.size()) return fail(Status::Truncated, text_.size());
                if (text_[i + 1] != 'u') return fail(Status::BadUnicode, i - 6);
                const int second = hex_quad(text_, i + 2);
                if (second < 0) {
                    const auto why = why_not_hex(text_, i + 2);
                    return fail(why, why == Status::Truncated ? text_.size() : i);
                }
                if (!low_surrogate(second)) return fail(Status::BadUnicode, i - 6);
                i += 6;
            }
            continue;
        }
        const auto sequence = utf8_sequence(text_, i);
        if (sequence == 0) return fail(Status::BadUnicode, i);
        i += sequence;
    }
    length = i - offset_ - 1;
    return Status::Ok;
}

Status Scanner::scan_number(std::size_t& length, bool& integral) {
    // offset_ is at '-' or a digit. RFC 8259: -? int frac? exp?, where int is
    // 0 or [1-9][0-9]*, frac is . [0-9]+, exp is [eE] [+-]? [0-9]+.
    std::size_t i = offset_;
    integral = true;
    if (text_[i] == '-') ++i;
    if (i >= text_.size()) return fail(Status::Truncated, text_.size());
    if (text_[i] == '0') {
        ++i;
    } else if (text_[i] >= '1' && text_[i] <= '9') {
        while (i < text_.size() && is_digit(text_[i])) ++i;
    } else {
        return fail(Status::Malformed, i);
    }
    if (i < text_.size() && is_digit(text_[i])) return fail(Status::Malformed, i);
    if (i < text_.size() && text_[i] == '.') {
        integral = false;
        ++i;
        if (i >= text_.size()) return fail(Status::Truncated, text_.size());
        if (!is_digit(text_[i])) return fail(Status::Malformed, i);
        while (i < text_.size() && is_digit(text_[i])) ++i;
    }
    if (i < text_.size() && (text_[i] == 'e' || text_[i] == 'E')) {
        integral = false;
        ++i;
        if (i < text_.size() && (text_[i] == '+' || text_[i] == '-')) ++i;
        if (i >= text_.size()) return fail(Status::Truncated, text_.size());
        if (!is_digit(text_[i])) return fail(Status::Malformed, i);
        while (i < text_.size() && is_digit(text_[i])) ++i;
    }
    length = i - offset_;
    return Status::Ok;
}

Status Scanner::scan_literal(std::string_view word) {
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (offset_ + i >= text_.size()) return fail(Status::Truncated, text_.size());
        if (text_[offset_ + i] != word[i]) return fail(Status::Malformed, offset_ + i);
    }
    return Status::Ok;
}

Status Scanner::scan_value(Event& event, bool as_key) {
    // offset_ is at the first byte of a value. On Ok, event describes it and
    // the cursor is past it; the caller records what that means for the
    // enclosing level.
    const char c = text_[offset_];
    const std::size_t start = offset_;
    Event scanned{};
    scanned.offset = start;
    scanned.depth = depth_;
    if (as_key && c != '"') return fail(Status::Malformed, start);
    switch (c) {
        case '{':
        case '[': {
            if (depth_ >= maximum_depth) return fail(Status::TooDeep, start);
            const auto level = bit(depth_);
            if (c == '{') objects_ |= level; else objects_ &= ~level;
            commas_ &= ~level;
            colons_ &= ~level;
            ++depth_;
            scanned.token = c == '{' ? Token::ObjectBegin : Token::ArrayBegin;
            scanned.length = 1;
            advance(1);
            break;
        }
        case '"': {
            std::size_t length = 0;
            const auto status = scan_string(length);
            if (status != Status::Ok) return status;
            scanned.token = as_key ? Token::Key : Token::String;
            scanned.offset = start + 1;
            scanned.length = length;
            advance(length + 2);
            break;
        }
        case 't':
        case 'f':
        case 'n': {
            const std::string_view word = c == 't' ? "true" : c == 'f' ? "false" : "null";
            const auto status = scan_literal(word);
            if (status != Status::Ok) return status;
            scanned.token = c == 't' ? Token::True : c == 'f' ? Token::False : Token::Null;
            scanned.length = word.size();
            advance(word.size());
            break;
        }
        default: {
            if (c != '-' && !is_digit(c)) return fail(Status::Malformed, start);
            std::size_t length = 0;
            bool integral = true;
            const auto status = scan_number(length, integral);
            if (status != Status::Ok) return status;
            scanned.token = integral ? Token::Integer : Token::Number;
            scanned.length = length;
            advance(length);
            break;
        }
    }
    event = scanned;
    return Status::Ok;
}

Status Scanner::next(Event& event) {
    if (issue_.status != Status::Ok) return issue_.status;
    skip_whitespace();

    // The end of the input.
    if (offset_ >= text_.size()) {
        if (depth_ == 0 && value_seen_) {
            Event end{};
            end.offset = text_.size();
            event = end;
            last_ = end;
            return Status::Ok;
        }
        return fail(Status::Truncated, text_.size());
    }

    // The top level: one value, then nothing.
    if (depth_ == 0) {
        if (value_seen_) return fail(Status::Malformed, offset_);
        Event scanned{};
        const auto status = scan_value(scanned, false);
        if (status != Status::Ok) return status;
        if (scanned.token != Token::ObjectBegin && scanned.token != Token::ArrayBegin)
            value_seen_ = true;
        event = scanned;
        last_ = scanned;
        return Status::Ok;
    }

    const unsigned int level_index = depth_ - 1;
    const auto level = bit(level_index);
    const bool in_object = (objects_ & level) != 0;
    const bool has_value = (commas_ & level) != 0;
    const bool awaiting_value = (colons_ & level) != 0;
    bool just_comma = false;
    char c = text_[offset_];

    // A comma is not a token: it is consumed with what follows it, and what
    // follows must be an item, so a trailing comma fails at the bracket.
    if (c == ',') {
        if (!has_value || awaiting_value) return fail(Status::Malformed, offset_);
        commas_ &= ~level;
        just_comma = true;
        advance(1);
        skip_whitespace();
        if (offset_ >= text_.size()) return fail(Status::Truncated, text_.size());
        c = text_[offset_];
    }

    // A close: allowed when nothing is half done at this level.
    if (c == ']' || c == '}') {
        if (just_comma || awaiting_value) return fail(Status::Malformed, offset_);
        if ((c == '}') != in_object) return fail(Status::Malformed, offset_);
        Event closed{};
        closed.token = in_object ? Token::ObjectEnd : Token::ArrayEnd;
        closed.offset = offset_;
        closed.length = 1;
        closed.depth = level_index;
        --depth_;
        objects_ &= ~level;
        commas_ &= ~level;
        colons_ &= ~level;
        if (depth_ == 0) {
            value_seen_ = true;
        } else {
            const auto parent = bit(depth_ - 1);
            commas_ |= parent;
            colons_ &= ~parent;
        }
        advance(1);
        event = closed;
        last_ = closed;
        return Status::Ok;
    }

    // An item where one is expected: after the open, after a comma, or a
    // key's value after its colon. Two items in a row is the missing comma.
    if (has_value && !just_comma) return fail(Status::Malformed, offset_);

    if (in_object && !awaiting_value) {
        Event key{};
        const auto status = scan_value(key, true);
        if (status != Status::Ok) return status;
        colons_ |= level;
        event = key;
        last_ = key;
        return Status::Ok;
    }

    if (in_object) {
        if (c != ':') return fail(Status::Malformed, offset_);
        advance(1);
        skip_whitespace();
        if (offset_ >= text_.size()) return fail(Status::Truncated, text_.size());
        c = text_[offset_];
        if (c == ',' || c == '}' || c == ']') return fail(Status::Malformed, offset_);
    }

    Event scanned{};
    const auto status = scan_value(scanned, false);
    if (status != Status::Ok) return status;
    colons_ &= ~level;
    if (scanned.token != Token::ObjectBegin && scanned.token != Token::ArrayBegin)
        commas_ |= level;
    event = scanned;
    last_ = scanned;
    return Status::Ok;
}

Status Scanner::skip() {
    if (issue_.status != Status::Ok) return issue_.status;
    const auto opened = last_.token;
    if (opened == Token::Key) {
        // The key's value, whatever it is, then whatever that contained.
        Event value{};
        const auto status = next(value);
        if (status != Status::Ok) return status;
        if (value.token != Token::ObjectBegin && value.token != Token::ArrayBegin)
            return Status::Ok;
        return skip();
    }
    if (opened != Token::ObjectBegin && opened != Token::ArrayBegin) return Status::Ok;
    const auto target = last_.depth;
    while (true) {
        Event event{};
        const auto status = next(event);
        if (status != Status::Ok) return status;
        if ((event.token == Token::ObjectEnd || event.token == Token::ArrayEnd) &&
            event.depth == target)
            return Status::Ok;
    }
}

Decoded unescape(std::string_view escaped, std::span<char> out) {
    const auto measured = decode(escaped, {});
    if (measured.status != Status::Ok) return measured;
    if (measured.count > out.size()) return {Status::Overflow, measured.count};
    if (measured.count == 0) return measured;
    return decode(escaped, out);
}

Status unescape_into(std::string_view escaped, std::string& out) {
    const auto measured = decode(escaped, {});
    if (measured.status != Status::Ok) return measured.status;
    std::string decoded(measured.count, '\0');
    if (measured.count != 0) {
        const auto written = decode(escaped, std::span<char>{decoded.data(), decoded.size()});
        if (written.status != Status::Ok) return written.status;
    }
    out = std::move(decoded);
    return Status::Ok;
}

Status integer(std::string_view digits, long long& value) {
    for (const char c : digits)
        if (c == '.' || c == 'e' || c == 'E') return Status::BadNumber;
    long long parsed = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
        return Status::BadNumber;
    value = parsed;
    return Status::Ok;
}

namespace {

// Whether an out-of-range number is beneath double rather than beyond it:
// the decimal exponent of its leading significant digit, from the digits
// and the exponent field, is negative. digits is a token the scanner
// accepted, so its shape is the grammar's.
[[nodiscard]] bool underflows(std::string_view digits) {
    std::size_t i = 0;
    bool negative = false;
    if (i < digits.size() && digits[i] == '-') { negative = true; ++i; }
    (void)negative;
    long long leading = 0;       // significant digits before the point
    bool significant = false;
    long long zeros_after_point = 0;
    bool any_nonzero = false;
    while (i < digits.size() && is_digit(digits[i])) {
        if (digits[i] != '0') { significant = true; any_nonzero = true; }
        if (significant) ++leading;
        ++i;
    }
    if (i < digits.size() && digits[i] == '.') {
        ++i;
        while (i < digits.size() && is_digit(digits[i])) {
            if (!significant) {
                if (digits[i] == '0') ++zeros_after_point;
                else { significant = true; any_nonzero = true; }
            }
            ++i;
        }
    }
    long long exponent = 0;
    if (i < digits.size() && (digits[i] == 'e' || digits[i] == 'E')) {
        ++i;
        bool negative_exponent = false;
        if (i < digits.size() && (digits[i] == '+' || digits[i] == '-')) {
            negative_exponent = digits[i] == '-';
            ++i;
        }
        while (i < digits.size() && is_digit(digits[i])) {
            if (exponent < 1'000'000'000) exponent = exponent * 10 + (digits[i] - '0');
            ++i;
        }
        if (negative_exponent) exponent = -exponent;
    }
    if (!any_nonzero) return true;
    const long long magnitude = leading > 0 ? leading - 1 : -(zeros_after_point + 1);
    return magnitude + exponent < 0;
}

}  // namespace

Status number(std::string_view digits, double& value) {
    // libc++ before its floating-point from_chars implementation is complete
    // declares that overload but deletes it.  strtod provides the same
    // complete-token conversion on those hosts while retaining our explicit
    // underflow policy.
    std::string input(digits);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(input.c_str(), &end);
    if (end != input.c_str() + input.size()) return Status::BadNumber;
    if (errno == ERANGE) {
        if (!underflows(digits)) return Status::BadNumber;
        value = digits.starts_with('-') ? -0.0 : 0.0;
        return Status::Ok;
    }
    value = parsed;
    return Status::Ok;
}

}  // namespace mm::json
