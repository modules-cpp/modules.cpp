// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module mm.shell:arithmetic;

import :state;

export namespace mm::shell {

enum class ArithmeticStatus {
    Ok,
    Syntax,
    Range,
    DivideByZero,
    Unset,
};

struct ArithmeticResult {
    ArithmeticStatus status = ArithmeticStatus::Ok;
    std::int64_t value = 0;
    std::size_t issue_offset = 0;

    [[nodiscard]] constexpr bool ok() const {
        return status == ArithmeticStatus::Ok;
    }
};

enum class NumericOperator { Eq, Ne, Lt, Le, Gt, Ge };

struct NumericTestResult {
    ArithmeticStatus status = ArithmeticStatus::Ok;
    bool value = false;
};

[[nodiscard]] ArithmeticResult parse_decimal(std::string_view text);
[[nodiscard]] ArithmeticResult evaluate_arithmetic(
    std::string_view expression, const ShellState& state);
[[nodiscard]] NumericTestResult numeric_test(
    NumericOperator operation, std::string_view left,
    std::string_view right);

}  // namespace mm::shell
