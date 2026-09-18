// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.json;
import mm.test;

namespace {

using mm::json::Event;
using mm::json::Scanner;
using mm::json::Status;
using mm::json::Token;
using mm::test::expect;

// Runs the scanner over text and answers the fault, or Ok with the tokens.
struct Walk {
    Status status = Status::Ok;
    std::size_t offset = 0;
    unsigned int line = 0;
    unsigned int column = 0;
    std::vector<Token> tokens;
};

[[nodiscard]] Walk walk(std::string_view text) {
    Walk result;
    Scanner scanner(text);
    while (true) {
        Event event{};
        const auto status = scanner.next(event);
        if (status != Status::Ok) {
            result.status = status;
            result.offset = scanner.issue().offset;
            result.line = scanner.issue().line;
            result.column = scanner.issue().column;
            return result;
        }
        result.tokens.push_back(event.token);
        if (event.token == Token::End) return result;
    }
}

[[nodiscard]] bool fails_at(std::string_view text, Status status, std::size_t offset) {
    const auto result = walk(text);
    return result.status == status && result.offset == offset;
}

[[nodiscard]] bool tokens_are(std::string_view text, std::vector<Token> expected) {
    const auto result = walk(text);
    return result.status == Status::Ok && result.tokens == expected;
}

void grammar_of_containers() {
    using T = Token;
    expect(tokens_are("[]", {T::ArrayBegin, T::ArrayEnd, T::End}), "empty array");
    expect(tokens_are("{}", {T::ObjectBegin, T::ObjectEnd, T::End}), "empty object");
    expect(tokens_are("[1,2]", {T::ArrayBegin, T::Integer, T::Integer, T::ArrayEnd, T::End}),
           "array of two");
    expect(tokens_are("{\"a\":1,\"b\":2}",
                      {T::ObjectBegin, T::Key, T::Integer, T::Key, T::Integer, T::ObjectEnd,
                       T::End}),
           "object of two");
    expect(tokens_are("[[]]", {T::ArrayBegin, T::ArrayBegin, T::ArrayEnd, T::ArrayEnd, T::End}),
           "nested arrays");
    expect(tokens_are("{\"a\":{}}",
                      {T::ObjectBegin, T::Key, T::ObjectBegin, T::ObjectEnd, T::ObjectEnd,
                       T::End}),
           "nested object");
    expect(tokens_are(" \t\n\r1 ", {T::Integer, T::End}), "the four whitespace characters");
    expect(fails_at("[1 2]", Status::Malformed, 3), "a missing comma fails at the item");
    expect(fails_at("[1,]", Status::Malformed, 3), "a trailing comma fails at the bracket");
    expect(fails_at("{\"a\":1,}", Status::Malformed, 7), "a trailing comma in an object");
    expect(fails_at("{\"a\"}", Status::Malformed, 4), "a key without a colon");
    expect(fails_at("{\"a\":}", Status::Malformed, 5), "a colon without a value");
    expect(fails_at("{1:2}", Status::Malformed, 1), "a key must be a string");
    expect(fails_at("[1}", Status::Malformed, 2), "the wrong closing bracket");
    expect(fails_at("1 2", Status::Malformed, 2), "a second top-level value");
    expect(fails_at("", Status::Truncated, 0), "empty input");
    expect(fails_at("   ", Status::Truncated, 3), "input that is only whitespace");
    expect(fails_at("[1,", Status::Truncated, 3), "input ending after a comma");
    expect(fails_at("{\"a\"", Status::Truncated, 4), "input ending after a key");
    expect(fails_at(",1", Status::Malformed, 0), "a leading comma");
    expect(fails_at("[,1]", Status::Malformed, 1), "a comma before the first item");
    expect(tokens_are("\xEF\xBB\xBF[]", {T::ArrayBegin, T::ArrayEnd, T::End}),
           "a leading byte order mark is skipped");
}

void grammar_of_numbers() {
    using T = Token;
    for (const char* text : {"0", "-0", "1", "-1", "123", "1e5", "1E+5", "1e-5", "1.5",
                             "-1.5e10", "0.1", "0e0"}) {
        const auto result = walk(text);
        expect(result.status == Status::Ok, std::string("number accepted: ") + text);
    }
    expect(tokens_are("[1,-0,10]", {T::ArrayBegin, T::Integer, T::Integer, T::Integer,
                                     T::ArrayEnd, T::End}),
           "integers are Integer tokens");
    expect(tokens_are("[1.0,1e5,-0.0]", {T::ArrayBegin, T::Number, T::Number, T::Number,
                                          T::ArrayEnd, T::End}),
           "fractions and exponents are Number tokens");
    expect(fails_at("01", Status::Malformed, 1), "a leading zero");
    expect(fails_at(".5", Status::Malformed, 0), "a leading point");
    expect(fails_at("1.", Status::Truncated, 2), "a trailing point at the end");
    expect(fails_at("[1.]", Status::Malformed, 3), "a trailing point in a container");
    expect(fails_at("+1", Status::Malformed, 0), "a leading plus");
    expect(fails_at("NaN", Status::Malformed, 0), "NaN");
    expect(fails_at("Infinity", Status::Malformed, 0), "Infinity");
    expect(fails_at("-", Status::Truncated, 1), "a lone minus");
    expect(fails_at("1e", Status::Truncated, 2), "an exponent without digits");
    expect(fails_at("1e+", Status::Truncated, 3), "an exponent sign without digits");
    expect(fails_at("0x10", Status::Malformed, 1), "a hexadecimal number");
    expect(fails_at("tru", Status::Truncated, 3), "a cut literal");
    expect(fails_at("trux", Status::Malformed, 3), "a wrong literal");
}

void grammar_of_strings() {
    using T = Token;
    expect(tokens_are("\"\"", {T::String, T::End}), "the empty string");
    expect(tokens_are("\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"", {T::String, T::End}),
           "the eight short escapes");
    expect(tokens_are("\"\\u00e9\"", {T::String, T::End}), "a \\u escape");
    expect(tokens_are("\"\\ud83d\\ude00\"", {T::String, T::End}), "a surrogate pair");
    expect(tokens_are("\"\xC3\xA9\"", {T::String, T::End}), "raw two-byte UTF-8");
    expect(tokens_are("\"\xF0\x9F\x98\x80\"", {T::String, T::End}), "raw four-byte UTF-8");
    expect(fails_at("\"a\nb\"", Status::Malformed, 2), "a raw newline in a string");
    expect(fails_at("\"\x01\"", Status::Malformed, 1), "a raw control character");
    expect(fails_at("\"\\x41\"", Status::BadEscape, 1), "an unknown escape");
    expect(fails_at("\"\\U0041\"", Status::BadEscape, 1), "a capital U escape");
    expect(fails_at("\"\\u12\"", Status::BadEscape, 1), "a short \\u escape");
    expect(fails_at("\"\\ud800\"", Status::BadUnicode, 1), "a lone high surrogate");
    expect(fails_at("\"\\udc00\"", Status::BadUnicode, 1), "a lone low surrogate");
    expect(fails_at("\"\\udc00\\ud800\"", Status::BadUnicode, 1), "an inverted pair");
    expect(fails_at("\"\\ud800abc\"", Status::BadUnicode, 1),
           "a high surrogate followed by text");
    expect(fails_at("\"\xC0\xAF\"", Status::BadUnicode, 1), "an overlong encoding");
    expect(fails_at("\"\xED\xA0\x80\"", Status::BadUnicode, 1), "an encoded surrogate");
    expect(fails_at("\"\xF4\x90\x80\x80\"", Status::BadUnicode, 1), "past U+10FFFF");
    expect(fails_at("\"\xC3\"", Status::BadUnicode, 1), "a truncated sequence");
    expect(fails_at("\"\x80\"", Status::BadUnicode, 1), "a lone continuation byte");
    expect(fails_at("\"abc", Status::Truncated, 4), "an unterminated string");
    expect(fails_at("\"\\", Status::Truncated, 2), "an unterminated escape");
    // The same faults through skip, so a scan-only reader refuses them too.
    Scanner scanner("[\"\\ud800\"]");
    Event event{};
    expect(scanner.next(event) == Status::Ok && event.token == T::ArrayBegin, "array opens");
    expect(scanner.skip() == Status::BadUnicode && scanner.issue().offset == 2,
           "skip refuses a lone surrogate at the same offset");
}

void depth_limit() {
    std::string sixty_four(64, '[');
    sixty_four += std::string(64, ']');
    expect(walk(sixty_four).status == Status::Ok, "sixty-four levels parse");
    std::string sixty_five(65, '[');
    sixty_five += std::string(65, ']');
    expect(fails_at(sixty_five, Status::TooDeep, 64), "sixty-five levels are TooDeep at the last");
}

void issue_positions() {
    const auto result = walk("[\n  1,\n  x\n]");
    expect(result.status == Status::Malformed && result.offset == 9 && result.line == 3 &&
               result.column == 3,
           "a fault on the third line is placed on it");
    Scanner scanner("[x]");
    Event event{};
    expect(scanner.next(event) == Status::Ok, "the array opens");
    expect(scanner.next(event) == Status::Malformed, "the fault is found");
    expect(scanner.issue().description == mm::json::describe(Status::Malformed),
           "the description is the fixed text for the status");
    expect(scanner.next(event) == Status::Malformed && scanner.issue().offset == 1,
           "a later call answers the same fault");
}

void next_leaves_the_event_alone_on_failure() {
    Scanner scanner("[1 2]");
    Event event{};
    expect(scanner.next(event) == Status::Ok && scanner.next(event) == Status::Ok,
           "two tokens read");
    const Event before = event;
    expect(scanner.next(event) == Status::Malformed, "the third fails");
    expect(event.token == before.token && event.offset == before.offset &&
               event.length == before.length && event.depth == before.depth,
           "the event is as the caller left it");
}

void skipping() {
    Scanner scanner("{\"a\":[1,{\"b\":2}],\"c\":3}");
    Event event{};
    expect(scanner.next(event) == Status::Ok && event.token == Token::ObjectBegin, "opens");
    expect(scanner.next(event) == Status::Ok && event.token == Token::Key, "first key");
    expect(scanner.skip() == Status::Ok, "the key's array value is skipped");
    expect(scanner.next(event) == Status::Ok && event.token == Token::Key &&
               std::string_view("{\"a\":[1,{\"b\":2}],\"c\":3}").substr(event.offset, event.length) == "c",
           "the next key follows");
    expect(scanner.next(event) == Status::Ok && event.token == Token::Integer, "its value");
    expect(scanner.skip() == Status::Ok, "skip after a scalar does nothing");
    expect(scanner.next(event) == Status::Ok && event.token == Token::ObjectEnd, "closes");
    Scanner nested("[[[1],[2]],3]");
    expect(nested.next(event) == Status::Ok && nested.next(event) == Status::Ok, "two opens");
    expect(nested.skip() == Status::Ok, "the inner array is skipped whole");
    expect(nested.next(event) == Status::Ok && event.token == Token::Integer &&
               event.offset == 11,
           "the item after it is next");
}

void unescaping() {
    std::array<char, 16> out{};
    auto decoded = mm::json::unescape("a\\u00e9\\n", out);
    expect(decoded.status == Status::Ok && decoded.count == 4 && out[0] == 'a' &&
               static_cast<unsigned char>(out[1]) == 0xC3 &&
               static_cast<unsigned char>(out[2]) == 0xA9 && out[3] == '\n',
           "escapes decode to UTF-8");
    std::array<char, 4> pair{};
    decoded = mm::json::unescape("\\ud83d\\ude00", pair);
    expect(decoded.status == Status::Ok && decoded.count == 4 &&
               static_cast<unsigned char>(pair[0]) == 0xF0 &&
               static_cast<unsigned char>(pair[3]) == 0x80,
           "a surrogate pair decodes to four bytes");
    std::array<char, 2> raw{};
    decoded = mm::json::unescape("\xC3\xA9", raw);
    expect(decoded.status == Status::Ok && decoded.count == 2 && raw == std::array<char, 2>{'\xC3', '\xA9'},
           "a raw character and its escape give the same bytes");
    std::array<char, 3> sentinel{'x', 'y', 'z'};
    decoded = mm::json::unescape("abcd", sentinel);
    expect(decoded.status == Status::Overflow && decoded.count == 4 &&
               sentinel == std::array<char, 3>{'x', 'y', 'z'},
           "a short span answers Overflow with the count and leaves the span alone");
    std::array<char, 4> enough{};
    expect(mm::json::unescape("abcd", enough).status == Status::Ok, "a span of that size is Ok");
    decoded = mm::json::unescape("\\ud800", sentinel);
    expect(decoded.status == Status::BadUnicode && decoded.count == 0 &&
               sentinel == std::array<char, 3>{'x', 'y', 'z'},
           "a lone surrogate handed to unescape is refused and the span untouched");
    decoded = mm::json::unescape("\\q", sentinel);
    expect(decoded.status == Status::BadEscape, "a bad escape handed to unescape is refused");
    decoded = mm::json::unescape("", enough);
    expect(decoded.status == Status::Ok && decoded.count == 0, "the empty string decodes to nothing");
}

void numeric_helpers() {
    long long integral = 77;
    expect(mm::json::integer("9223372036854775807", integral) == Status::Ok &&
               integral == 9223372036854775807LL,
           "the largest long long");
    expect(mm::json::integer("-0", integral) == Status::Ok && integral == 0,
           "integer -0 is 0");
    integral = 77;
    expect(mm::json::integer("9223372036854775808", integral) == Status::BadNumber &&
               integral == 77,
           "one past long long is BadNumber and the output is untouched");
    expect(mm::json::integer("1.0", integral) == Status::BadNumber && integral == 77,
           "a fraction is BadNumber for integer");
    expect(mm::json::integer("1e2", integral) == Status::BadNumber && integral == 77,
           "an exponent is BadNumber for integer");
    double real = 7.5;
    expect(mm::json::number("1.5", real) == Status::Ok && real == 1.5, "a fraction");
    expect(mm::json::number("9223372036854775809", real) == Status::Ok &&
               real == 9223372036854775808.0,
           "number rounds a wide integer");
    real = 7.5;
    expect(mm::json::number("1e400", real) == Status::BadNumber && real == 7.5,
           "overflow is BadNumber and the output is untouched");
    expect(mm::json::number("-1e400", real) == Status::BadNumber && real == 7.5,
           "negative overflow too");
    expect(mm::json::number("1e-400", real) == Status::Ok && real == 0.0 && !std::signbit(real),
           "underflow is zero");
    expect(mm::json::number("-123e-10000000", real) == Status::Ok && real == 0.0 &&
               std::signbit(real),
           "negative underflow is negative zero");
    expect(mm::json::number("123.456e-789", real) == Status::Ok && real == 0.0,
           "a fractional underflow is zero");
}

const mm::test::case_ cases[] = {
    {"grammar of containers", &grammar_of_containers},
    {"grammar of numbers", &grammar_of_numbers},
    {"grammar of strings", &grammar_of_strings},
    {"depth limit", &depth_limit},
    {"issue positions", &issue_positions},
    {"next leaves the event alone on failure", &next_leaves_the_event_alone_on_failure},
    {"skipping", &skipping},
    {"unescaping", &unescaping},
    {"numeric helpers", &numeric_helpers},
};

const mm::test::registrar reg{"mm.json scan", cases};

}
