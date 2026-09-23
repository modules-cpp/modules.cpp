// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::ArithmeticStatus;
using mm::shell::ShellState;
using mm::test::expect;

void decimal_boundaries() {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    expect(mm::shell::parse_decimal("9223372036854775807").value == maximum,
           "maximum signed decimal parses");
    expect(mm::shell::parse_decimal("-9223372036854775808").value == minimum,
           "minimum signed decimal parses");
    constexpr std::array<std::string_view, 4> overflow{
        "9223372036854775808", "-9223372036854775809",
        "999999999999999999999999999999", "+9223372036854775808"};
    for (const auto value : overflow) {
        expect(mm::shell::parse_decimal(value).status ==
                   ArithmeticStatus::Range,
               "out-of-range decimal is refused");
    }
    constexpr std::array<std::string_view, 4> malformed{
        "", "+", "12x", " 1"};
    for (const auto value : malformed) {
        expect(mm::shell::parse_decimal(value).status ==
                   ArithmeticStatus::Syntax,
               "non-decimal input is refused");
    }
}

void precedence_and_variables() {
    mm::shell::VariableSlot slots[2]{};
    char text[32]{};
    ShellState state{slots, text, {}, {}};
    expect(state.assign("x", "12").ok(), "integer variable installed");
    expect(mm::shell::evaluate_arithmetic("1 + 2 * 3", state).value == 7,
           "multiplication binds before addition");
    expect(mm::shell::evaluate_arithmetic("(1+2)*3", state).value == 9,
           "parentheses override precedence");
    expect(mm::shell::evaluate_arithmetic("-x + +4", state).value == -8,
           "unary signs and variable lookup work");
    expect(mm::shell::evaluate_arithmetic("-7 / 3", state).value == -2 &&
               mm::shell::evaluate_arithmetic("-7 % 3", state).value == -1,
           "division truncates toward zero with dividend-signed remainder");
    expect(mm::shell::evaluate_arithmetic("missing+1", state).value == 1,
           "unset variable is zero without nounset");
    state.nounset = true;
    expect(mm::shell::evaluate_arithmetic("missing+1", state).status ==
               ArithmeticStatus::Unset,
           "nounset rejects absent arithmetic variable");
}

void checked_operations() {
    const ShellState state;
    constexpr std::array<std::string_view, 9> overflow{
        "9223372036854775807+1",
        "-9223372036854775808-1",
        "3037000500*3037000500",
        "-9223372036854775808/-1",
        "-9223372036854775808%-1",
        "--9223372036854775808",
        "-(-9223372036854775808)",
        "9223372036854775808",
        "-9223372036854775809"};
    for (const auto expression : overflow) {
        expect(mm::shell::evaluate_arithmetic(expression, state).status ==
                   ArithmeticStatus::Range,
               "arithmetic overflow is detected before operation");
    }
    expect(mm::shell::evaluate_arithmetic("1/0", state).status ==
               ArithmeticStatus::DivideByZero &&
               mm::shell::evaluate_arithmetic("1%0", state).status ==
                   ArithmeticStatus::DivideByZero,
           "division and remainder by zero are typed failures");
    expect(mm::shell::evaluate_arithmetic("2**3", state).status ==
               ArithmeticStatus::Syntax &&
               mm::shell::evaluate_arithmetic("1<<2", state).status ==
                   ArithmeticStatus::Syntax,
           "excluded operators are syntax errors");
}

void numeric_test_boundaries() {
    using mm::shell::NumericOperator;
    expect(mm::shell::numeric_test(NumericOperator::Lt, "-2", "1").value,
           "numeric less-than uses signed comparison");
    expect(mm::shell::numeric_test(NumericOperator::Ge, "1", "1").value,
           "numeric greater-equal includes equality");
    expect(mm::shell::numeric_test(NumericOperator::Eq,
                                   "9223372036854775808", "0").status ==
               ArithmeticStatus::Range,
           "numeric test shares 64-bit conversion checks");
}

const mm::test::case_ cases[]{
    {"decimal boundaries", &decimal_boundaries},
    {"precedence and variables", &precedence_and_variables},
    {"checked operations", &checked_operations},
    {"numeric test boundaries", &numeric_test_boundaries},
};

const mm::test::registrar reg{"mm.shell arithmetic", cases};

}  // namespace
