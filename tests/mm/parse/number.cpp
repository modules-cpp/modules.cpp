// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdint>
#include <limits>
#include <string_view>

import mm.parse;
import mm.test;

namespace {

using mm::parse::NumberKind;
using mm::parse::NumberValue;
using mm::parse::parse_number;
using mm::parse::parse_number_at;
using mm::test::expect;

// --- Integer tests ---

void test_integer_decimal() {
    const auto result = parse_number("42");
    expect(result.kind == NumberKind::Integer, "42 is Integer");
    expect(result.integer == 42, "42 value");
    expect(result.length == 2, "42 length");
    expect(!result.overflow, "42 no overflow");
}

void test_integer_negative() {
    const auto result = parse_number("-42");
    expect(result.kind == NumberKind::Integer, "-42 is Integer");
    expect(result.integer == -42, "-42 value");
}

void test_integer_plus_sign() {
    const auto result = parse_number("+42");
    expect(result.kind == NumberKind::Integer, "+42 is Integer");
    expect(result.integer == 42, "+42 value");
}

void test_integer_zero() {
    const auto result = parse_number("0");
    expect(result.kind == NumberKind::Integer, "0 is Integer");
    expect(result.integer == 0, "0 value");
}

void test_integer_leading_whitespace() {
    const auto result = parse_number("  42");
    expect(result.kind == NumberKind::Integer, "  42 is Integer");
    expect(result.integer == 42, "  42 value");
    expect(result.consumed == 4, "  42 consumed includes whitespace");
}

void test_integer_hex() {
    const auto result = parse_number("0xFF");
    expect(result.kind == NumberKind::Integer, "0xFF is Integer");
    expect(result.integer == 255, "0xFF value is 255");
}

void test_integer_hex_negative() {
    const auto result = parse_number("-0x1F");
    expect(result.kind == NumberKind::Integer, "-0x1F is Integer");
    expect(result.integer == -31, "-0x1F value is -31");
}

void test_integer_octal() {
    const auto result = parse_number("0o777");
    expect(result.kind == NumberKind::Integer, "0o777 is Integer");
    expect(result.integer == 511, "0o777 value is 511");
}

void test_integer_binary() {
    const auto result = parse_number("0b1010");
    expect(result.kind == NumberKind::Integer, "0b1010 is Integer");
    expect(result.integer == 10, "0b1010 value is 10");
}

void test_integer_underscore_separators() {
    const auto result = parse_number("1_000_000");
    expect(result.kind == NumberKind::Integer, "1_000_000 is Integer");
    expect(result.integer == 1000000, "1_000_000 value");
}

void test_integer_overflow() {
    const auto result = parse_number("99999999999999999999");
    expect(result.kind == NumberKind::Integer, "large number is Integer");
    expect(result.overflow, "large number overflows");
    expect(result.integer == std::numeric_limits<std::int64_t>::max(),
           "overflow clamps to int64 max");
}

void test_integer_invalid_no_digits() {
    const auto result = parse_number("abc");
    expect(result.kind == NumberKind::Invalid, "abc is Invalid");
}

void test_integer_invalid_sign_only() {
    const auto result = parse_number("+");
    expect(result.kind == NumberKind::Invalid, "+ is Invalid");
}

void test_integer_invalid_trailing_garbage() {
    const auto result = parse_number("42abc");
    // 42 is a valid integer; the parser stops at 'a'.
    expect(result.kind == NumberKind::Integer, "42abc: 42 is Integer");
    expect(result.integer == 42, "42abc: value is 42");
    expect(result.length == 2, "42abc: length is 2 (stops at a)");
}

// --- Float tests ---

void test_float_basic() {
    const auto result = parse_number("3.14");
    expect(result.kind == NumberKind::Float, "3.14 is Float");
    expect(result.real > 3.13 && result.real < 3.15, "3.14 value");
}

void test_float_negative() {
    const auto result = parse_number("-3.14");
    expect(result.kind == NumberKind::Float, "-3.14 is Float");
    expect(result.real < -3.13, "-3.14 value");
}

void test_float_trailing_dot() {
    const auto result = parse_number("3.");
    expect(result.kind == NumberKind::Float, "3. is Float");
    expect(result.real > 2.9 && result.real < 3.1, "3. value is 3.0");
}

void test_float_leading_dot() {
    const auto result = parse_number(".5");
    expect(result.kind == NumberKind::Float, ".5 is Float");
    expect(result.real > 0.4 && result.real < 0.6, ".5 value");
}

void test_float_no_integer_part() {
    const auto result = parse_number("-.5");
    expect(result.kind == NumberKind::Float, "-.5 is Float");
    expect(result.real < -0.4, "-.5 value");
}

// --- Scientific tests ---

void test_scientific_basic() {
    const auto result = parse_number("1e10");
    expect(result.kind == NumberKind::Scientific, "1e10 is Scientific");
    expect(result.real == 10000000000.0, "1e10 value");
}

void test_scientific_negative_exponent() {
    const auto result = parse_number("1e-10");
    expect(result.kind == NumberKind::Scientific, "1e-10 is Scientific");
    expect(result.real > 0.0 && result.real < 1e-9, "1e-10 value is tiny");
}

void test_scientific_with_fraction() {
    const auto result = parse_number("2.5e3");
    expect(result.kind == NumberKind::Scientific, "2.5e3 is Scientific");
    expect(result.real == 2500.0, "2.5e3 value is 2500");
}

void test_scientific_capital_E() {
    const auto result = parse_number("1E10");
    expect(result.kind == NumberKind::Scientific, "1E10 is Scientific");
    expect(result.real == 10000000000.0, "1E10 value");
}

void test_scientific_negative_base() {
    const auto result = parse_number("-2.5e3");
    expect(result.kind == NumberKind::Scientific, "-2.5e3 is Scientific");
    expect(result.real == -2500.0, "-2.5e3 value is -2500");
}

// --- Invalid tests ---

void test_invalid_empty() {
    const auto result = parse_number("");
    expect(result.kind == NumberKind::Invalid, "empty is Invalid");
}

void test_invalid_whitespace_only() {
    const auto result = parse_number("   ");
    expect(result.kind == NumberKind::Invalid, "whitespace only is Invalid");
}

void test_invalid_multiple_signs() {
    const auto result = parse_number("--42");
    expect(result.kind == NumberKind::Invalid, "--42 is Invalid");
}

void test_invalid_exponent_no_digits() {
    const auto result = parse_number("1e+");
    expect(result.kind == NumberKind::Invalid, "1e+ is Invalid");
}

void test_invalid_hex_trailing() {
    const auto result = parse_number("0xGG");
    expect(result.kind == NumberKind::Invalid, "0xGG is Invalid");
}

// --- Edge cases ---

void test_edge_plus_zero() {
    const auto result = parse_number("+0");
    expect(result.kind == NumberKind::Integer, "+0 is Integer");
    expect(result.integer == 0, "+0 value is 0");
}

void test_edge_negative_zero() {
    const auto result = parse_number("-0");
    expect(result.kind == NumberKind::Integer, "-0 is Integer");
    expect(result.integer == 0, "-0 value is 0");
}

void test_edge_hex_large() {
    const auto result = parse_number("0xFFFFFFFF");
    expect(result.kind == NumberKind::Integer, "0xFFFFFFFF is Integer");
    expect(result.integer == 4294967295, "0xFFFFFFFF value");
}

void test_edge_binary_large() {
    const auto result = parse_number("0b11111111111111111111111111111111");
    expect(result.kind == NumberKind::Integer, "0b... is Integer");
    expect(result.integer == 4294967295, "0b... value is 2^32-1");
}

void test_edge_octal_large() {
    const auto result = parse_number("0o77777777777");
    expect(result.kind == NumberKind::Integer, "0o77777777777 is Integer");
    expect(result.integer == 8589934591, "0o77777777777 value");
}

// --- parse_number(text, at) tests ---

void test_parse_at_offset() {
    const std::string_view text = "abc 42 def";
    const auto result = parse_number_at(text, 4);
    expect(result.kind == NumberKind::Integer, "parse at offset 4");
    expect(result.integer == 42, "parse at offset 42");
    expect(result.offset == 4, "offset is 4");
}

void test_parse_at_offset_with_ws() {
    const std::string_view text = "abc   42 def";
    const auto result = parse_number_at(text, 3);
    expect(result.kind == NumberKind::Integer, "parse at offset 3");
    expect(result.integer == 42, "parse at offset 42");
}

void test_parse_at_end() {
    const std::string_view text = "abc";
    const auto result = parse_number_at(text, 3);
    expect(result.kind == NumberKind::Invalid, "parse at end is Invalid");
}

const mm::test::case_ cases[]{
    {"integer decimal", &test_integer_decimal},
    {"integer negative", &test_integer_negative},
    {"integer plus sign", &test_integer_plus_sign},
    {"integer zero", &test_integer_zero},
    {"integer leading whitespace", &test_integer_leading_whitespace},
    {"integer hex", &test_integer_hex},
    {"integer hex negative", &test_integer_hex_negative},
    {"integer octal", &test_integer_octal},
    {"integer binary", &test_integer_binary},
    {"integer underscore separators", &test_integer_underscore_separators},
    {"integer overflow", &test_integer_overflow},
    {"integer invalid no digits", &test_integer_invalid_no_digits},
    {"integer invalid sign only", &test_integer_invalid_sign_only},
    {"integer invalid trailing garbage", &test_integer_invalid_trailing_garbage},
    {"float basic", &test_float_basic},
    {"float negative", &test_float_negative},
    {"float trailing dot", &test_float_trailing_dot},
    {"float leading dot", &test_float_leading_dot},
    {"float no integer part", &test_float_no_integer_part},
    {"scientific basic", &test_scientific_basic},
    {"scientific negative exponent", &test_scientific_negative_exponent},
    {"scientific with fraction", &test_scientific_with_fraction},
    {"scientific capital E", &test_scientific_capital_E},
    {"scientific negative base", &test_scientific_negative_base},
    {"invalid empty", &test_invalid_empty},
    {"invalid whitespace only", &test_invalid_whitespace_only},
    {"invalid multiple signs", &test_invalid_multiple_signs},
    {"invalid exponent no digits", &test_invalid_exponent_no_digits},
    {"invalid hex trailing", &test_invalid_hex_trailing},
    {"edge +0", &test_edge_plus_zero},
    {"edge -0", &test_edge_negative_zero},
    {"edge hex large", &test_edge_hex_large},
    {"edge binary large", &test_edge_binary_large},
    {"edge octal large", &test_edge_octal_large},
    {"parse at offset", &test_parse_at_offset},
    {"parse at offset with ws", &test_parse_at_offset_with_ws},
    {"parse at end", &test_parse_at_end},
};

const mm::test::registrar reg{"mm.parse number", cases};

}  // namespace
