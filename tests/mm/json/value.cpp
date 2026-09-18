// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cmath>
#include <limits>
#include <cstddef>
#include <string>
#include <string_view>

import mm.json;
import mm.test;

namespace {

using mm::json::Duplicates;
using mm::json::Layout;
using mm::json::ParseOptions;
using mm::json::Status;
using mm::json::Type;
using mm::json::Value;
using mm::json::WideIntegers;
using mm::test::expect;

[[nodiscard]] std::string compact(const Value& value) {
    std::string out;
    (void)mm::json::write(value, out, Layout::Compact);
    return out;
}

[[nodiscard]] bool parses_to(std::string_view text, std::string_view written) {
    Value value;
    const auto outcome = mm::json::parse(text, value);
    return outcome.status == Status::Ok && compact(value) == written;
}

void the_value_model() {
    Value null;
    expect(null.is_null() && null.type() == Type::Null, "default is Null");
    bool flag = false;
    expect(Value(true).boolean(flag) && flag, "a boolean");
    long long integral = 0;
    expect(Value(42LL).integer(integral) && integral == 42, "an integer");
    double real = 0;
    expect(Value(42LL).number(real) && real == 42.0, "number answers an integer as a double");
    expect(Value(1.5).number(real) && real == 1.5, "a number");
    expect(!Value(1.5).integer(integral) && integral == 42, "integer refuses a number, untouched");
    expect(Value(std::string_view{"text"}).string() == "text", "a string");
    expect(Value(42LL).string().empty() && Value(42LL).items().empty() &&
               Value(42LL).members().empty() && Value(42LL).find("a") == nullptr,
           "the wrong accessors answer empty");
    Value array = Value::array();
    expect(array.push(Value(1LL)) && array.push(Value(2LL)) && array.items().size() == 2,
           "push on an array");
    Value object = Value::object();
    expect(object.set("a", Value(1LL)) && object.set("b", Value(2LL)) &&
               object.set("a", Value(3LL)) && object.members().size() == 2,
           "set appends and replaces in place");
    long long found = 0;
    expect(object.find("a") != nullptr && object.find("a")->integer(found) && found == 3 &&
               object.members()[0].key == "a",
           "set keeps the key's position");
    expect(object.find("c") == nullptr, "find answers null for an absent key");
}

void wrong_type_mutation() {
    Value samples[] = {Value(), Value(true), Value(1LL), Value(1.5),
                       Value(std::string_view{"s"}), Value::object()};
    for (auto& sample : samples) {
        const auto before = compact(sample);
        expect(!sample.push(Value(1LL)) && compact(sample) == before,
               "push on a non-array answers false and changes nothing");
    }
    Value others[] = {Value(), Value(true), Value(1LL), Value(1.5),
                      Value(std::string_view{"s"}), Value::array()};
    for (auto& sample : others) {
        const auto before = compact(sample);
        expect(!sample.set("k", Value(1LL)) && compact(sample) == before,
               "set on a non-object answers false and changes nothing");
    }
}

void parsing() {
    expect(parses_to("null", "null") && parses_to("true", "true") && parses_to("false", "false"),
           "the literals");
    expect(parses_to("42", "42") && parses_to("-7", "-7"), "integers");
    expect(parses_to("1.5", "1.5") && parses_to("1.5e3", "1500.0"), "numbers");
    expect(parses_to("\"a\\u00e9b\"", "\"a\xC3\xA9" "b\""), "a string with an escape");
    expect(parses_to("[1,[2,3],{\"a\":null}]", "[1,[2,3],{\"a\":null}]"), "a nested document");
    expect(parses_to("{\"b\":1,\"a\":2}", "{\"b\":1,\"a\":2}"), "member order is kept");
    Value value;
    auto outcome = mm::json::parse("{\"a\":1,\"b\":{\"c\":[true]}}", value);
    long long found = 0;
    bool flag = false;
    expect(outcome.status == Status::Ok && value.find("a") != nullptr &&
               value.find("a")->integer(found) && found == 1 &&
               value.find("b")->find("c")->items()[0].boolean(flag) && flag,
           "navigation through find and items");
    outcome = mm::json::parse("[1,2", value);
    expect(outcome.status == Status::Truncated && outcome.issue.offset == 4,
           "the scanner's fault comes back as the outcome");
    outcome = mm::json::parse("[\n  1,\n  x\n]", value);
    expect(outcome.status == Status::Malformed && outcome.issue.line == 3 &&
               outcome.issue.column == 3 &&
               outcome.issue.description == mm::json::describe(Status::Malformed),
           "the issue carries line, column, and the fixed description");
}

void numbers_and_types() {
    expect(parses_to("9223372036854775807", "9223372036854775807"), "the largest integer");
    expect(parses_to("1.0", "1.0") && parses_to("-0.0", "-0.0"),
           "a Number writes with its point and reads back as a Number");
    expect(parses_to("1", "1") && parses_to("-0", "0"),
           "integers write as they are, and -0 is 0");
    Value value;
    expect(mm::json::parse("1.0", value).status == Status::Ok && value.type() == Type::Number &&
               mm::json::parse("1", value).status == Status::Ok && value.type() == Type::Integer,
           "the type survives the round trip");
    auto outcome = mm::json::parse("9223372036854775809", value);
    expect(outcome.status == Status::BadNumber && outcome.issue.offset == 0,
           "one past long long, not a double, is BadNumber by default");
    outcome = mm::json::parse("9223372036854775808", value);
    expect(outcome.status == Status::BadNumber, "one past long long, a double, is too");
    ParseOptions approximate;
    approximate.wide_integers = WideIntegers::Approximate;
    double real = 0;
    outcome = mm::json::parse("9223372036854775809", value, approximate);
    expect(outcome.status == Status::Ok && value.type() == Type::Number && value.number(real) &&
               real == 9223372036854775808.0,
           "under Approximate it is the nearest double");
    outcome = mm::json::parse("1e400", value);
    expect(outcome.status == Status::BadNumber && outcome.issue.offset == 0, "overflow");
    outcome = mm::json::parse("[1e-400,-123e-10000000]", value);
    expect(outcome.status == Status::Ok && compact(value) == "[0.0,-0.0]",
           "underflow is zero with the sign");
}

void duplicates() {
    Value value;
    auto outcome = mm::json::parse("{\"a\":1,\"a\":2}", value);
    expect(outcome.status == Status::DuplicateKey && outcome.issue.offset == 7,
           "a repeated key is rejected at its opening quote by default");
    ParseOptions first;
    first.duplicates = Duplicates::KeepFirst;
    outcome = mm::json::parse("{\"a\":1,\"a\":{\"x\":[2]},\"b\":3}", value, first);
    expect(outcome.status == Status::Ok && compact(value) == "{\"a\":1,\"b\":3}",
           "KeepFirst keeps the first and walks the dropped container");
    ParseOptions last;
    last.duplicates = Duplicates::KeepLast;
    outcome = mm::json::parse("{\"a\":1,\"b\":2,\"a\":3}", value, last);
    expect(outcome.status == Status::Ok && compact(value) == "{\"a\":3,\"b\":2}",
           "KeepLast replaces in place");
}

void writing() {
    Value value;
    (void)mm::json::parse("{\"a\":[1,2.5,\"x\\ny\",null,true],\"b\":{}}", value);
    expect(compact(value) == "{\"a\":[1,2.5,\"x\\ny\",null,true],\"b\":{}}", "compact layout");
    std::string indented;
    expect(mm::json::write(value, indented, Layout::Indented).status == Status::Ok &&
               indented == "{\n  \"a\": [\n    1,\n    2.5,\n    \"x\\ny\",\n    null,\n    true\n  ],\n  \"b\": {}\n}",
           "indented layout");
    Value again;
    expect(mm::json::parse(indented, again).status == Status::Ok && again.equals(value),
           "the indented document reads back equal");
    Value control(std::string_view{"\x01\x1f\"\\"});
    expect(compact(control) == "\"\\u0001\\u001f\\\"\\\\\"", "control characters are escaped");
    Value nan(std::nan(""));
    std::string out = "kept";
    auto outcome = mm::json::write(nan, out);
    expect(outcome.status == Status::BadNumber && out == "kept", "NaN is BadNumber, out untouched");
    Value infinite(std::numeric_limits<double>::infinity());
    expect(mm::json::write(infinite, out).status == Status::BadNumber && out == "kept",
           "infinity is BadNumber");
    Value invalid(std::string_view{"\xff"});
    expect(mm::json::write(invalid, out).status == Status::BadUnicode && out == "kept",
           "an invalid string is BadUnicode, out untouched");
    Value deep = Value::array();
    Value* cursor = &deep;
    for (int i = 0; i < 63; ++i) {
        (void)cursor->push(Value::array());
        cursor = const_cast<Value*>(&cursor->items().back());
    }
    expect(mm::json::write(deep, out).status == Status::Ok, "sixty-four nested arrays write");
    (void)cursor->push(Value::array());
    out = "kept";
    expect(mm::json::write(deep, out).status == Status::TooDeep && out == "kept",
           "sixty-five answer TooDeep, out untouched");
}

void outputs_unchanged_on_failure() {
    Value sentinel(7LL);
    for (const char* text : {"[1,", "{\"a\":1,\"a\":2}", "1e400", "\"\\ud800\"", "x", ""}) {
        Value value = sentinel;
        const auto outcome = mm::json::parse(text, value);
        expect(outcome.status != Status::Ok && compact(value) == "7",
               std::string("parse leaves out untouched for: ") + text);
    }
}

const mm::test::case_ cases[] = {
    {"the value model", &the_value_model},
    {"wrong-type mutation", &wrong_type_mutation},
    {"parsing", &parsing},
    {"numbers and types", &numbers_and_types},
    {"duplicates", &duplicates},
    {"writing", &writing},
    {"outputs unchanged on failure", &outputs_unchanged_on_failure},
};

const mm::test::registrar reg{"mm.json value", cases};

}
