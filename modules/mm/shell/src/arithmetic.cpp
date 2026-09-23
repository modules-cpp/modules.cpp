// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

module mm.shell;

import :arithmetic;
import :state;

namespace mm::shell {
namespace {

constexpr auto signed_max = std::numeric_limits<std::int64_t>::max();
constexpr auto signed_min = std::numeric_limits<std::int64_t>::min();
constexpr auto negative_limit =
    static_cast<std::uint64_t>(signed_max) + 1;

[[nodiscard]] bool digit(char c) { return c >= '0' && c <= '9'; }

[[nodiscard]] bool name_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_rest(char c) {
    return name_first(c) || digit(c);
}

[[nodiscard]] ArithmeticResult magnitude(std::string_view text,
                                          bool negative) {
    if (text.empty()) return {ArithmeticStatus::Syntax, 0, 0};
    const auto limit = negative ? negative_limit
                                : static_cast<std::uint64_t>(signed_max);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!digit(text[i])) return {ArithmeticStatus::Syntax, 0, i};
        const auto next = static_cast<unsigned int>(text[i] - '0');
        if (value > (limit - next) / 10) {
            return {ArithmeticStatus::Range, 0, i};
        }
        value = value * 10 + next;
    }
    if (negative && value == negative_limit) {
        return {ArithmeticStatus::Ok, signed_min, 0};
    }
    const auto signed_value = static_cast<std::int64_t>(value);
    return {ArithmeticStatus::Ok,
            negative ? -signed_value : signed_value, 0};
}

[[nodiscard]] std::uint64_t absolute(std::int64_t value) {
    if (value >= 0) return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

[[nodiscard]] ArithmeticResult add(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > signed_max - b) ||
        (b < 0 && a < signed_min - b)) {
        return {ArithmeticStatus::Range};
    }
    return {ArithmeticStatus::Ok, a + b};
}

[[nodiscard]] ArithmeticResult subtract(std::int64_t a,
                                         std::int64_t b) {
    if ((b < 0 && a > signed_max + b) ||
        (b > 0 && a < signed_min + b)) {
        return {ArithmeticStatus::Range};
    }
    return {ArithmeticStatus::Ok, a - b};
}

[[nodiscard]] ArithmeticResult multiply(std::int64_t a,
                                         std::int64_t b) {
    const bool negative = (a < 0) != (b < 0);
    const auto left = absolute(a);
    const auto right = absolute(b);
    const auto limit = negative ? negative_limit
                                : static_cast<std::uint64_t>(signed_max);
    if (right != 0 && left > limit / right) {
        return {ArithmeticStatus::Range};
    }
    const auto product = left * right;
    if (negative && product == negative_limit) {
        return {ArithmeticStatus::Ok, signed_min};
    }
    const auto signed_value = static_cast<std::int64_t>(product);
    return {ArithmeticStatus::Ok,
            negative ? -signed_value : signed_value};
}

[[nodiscard]] ArithmeticResult divide(std::int64_t a, std::int64_t b,
                                       bool remainder) {
    if (b == 0) return {ArithmeticStatus::DivideByZero};
    if (a == signed_min && b == -1) {
        return {ArithmeticStatus::Range};
    }
    return {ArithmeticStatus::Ok, remainder ? a % b : a / b};
}

class Expression {
public:
    Expression(std::string_view text, const ShellState& state)
        : text_(text), state_(state) {}

    [[nodiscard]] ArithmeticResult run() {
        const auto result = sum();
        if (!result.ok()) return result;
        blanks();
        if (at_ != text_.size()) {
            return {ArithmeticStatus::Syntax, 0, at_};
        }
        return result;
    }

private:
    void blanks() {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' ||
                text_[at_] == '\n')) ++at_;
    }

    [[nodiscard]] ArithmeticResult sum() {
        auto left = product();
        if (!left.ok()) return left;
        while (true) {
            blanks();
            if (at_ == text_.size() ||
                (text_[at_] != '+' && text_[at_] != '-')) return left;
            const auto operation = text_[at_++];
            const auto right = product();
            if (!right.ok()) return right;
            left = operation == '+' ? add(left.value, right.value)
                                    : subtract(left.value, right.value);
            if (!left.ok()) {
                left.issue_offset = at_;
                return left;
            }
        }
    }

    [[nodiscard]] ArithmeticResult product() {
        auto left = unary();
        if (!left.ok()) return left;
        while (true) {
            blanks();
            if (at_ == text_.size() ||
                (text_[at_] != '*' && text_[at_] != '/' &&
                 text_[at_] != '%')) return left;
            const auto operation = text_[at_++];
            const auto right = unary();
            if (!right.ok()) return right;
            left = operation == '*'
                       ? multiply(left.value, right.value)
                       : divide(left.value, right.value,
                                operation == '%');
            if (!left.ok()) {
                left.issue_offset = at_;
                return left;
            }
        }
    }

    [[nodiscard]] ArithmeticResult unary() {
        blanks();
        if (at_ == text_.size()) {
            return {ArithmeticStatus::Syntax, 0, at_};
        }
        if (text_[at_] == '+' || text_[at_] == '-') {
            const bool negative = text_[at_++] == '-';
            const auto sign_at = at_ - 1;
            blanks();
            if (at_ < text_.size() && digit(text_[at_])) {
                const auto start = at_;
                while (at_ < text_.size() && digit(text_[at_])) ++at_;
                auto result = magnitude(text_.substr(start, at_ - start),
                                        negative);
                result.issue_offset += start;
                return result;
            }
            auto result = unary();
            if (!result.ok() || !negative) return result;
            if (result.value == signed_min) {
                return {ArithmeticStatus::Range, 0, sign_at};
            }
            result.value = -result.value;
            return result;
        }
        return primary();
    }

    [[nodiscard]] ArithmeticResult primary() {
        blanks();
        if (at_ == text_.size()) {
            return {ArithmeticStatus::Syntax, 0, at_};
        }
        if (text_[at_] == '(') {
            ++at_;
            const auto result = sum();
            if (!result.ok()) return result;
            blanks();
            if (at_ == text_.size() || text_[at_] != ')') {
                return {ArithmeticStatus::Syntax, 0, at_};
            }
            ++at_;
            return result;
        }
        if (digit(text_[at_])) {
            const auto start = at_;
            while (at_ < text_.size() && digit(text_[at_])) ++at_;
            auto result = magnitude(text_.substr(start, at_ - start), false);
            result.issue_offset += start;
            return result;
        }
        if (name_first(text_[at_])) {
            const auto start = at_++;
            while (at_ < text_.size() && name_rest(text_[at_])) ++at_;
            const auto name = text_.substr(start, at_ - start);
            const auto value = state_.lookup(name);
            if (!value.found) {
                if (state_.nounset) {
                    return {ArithmeticStatus::Unset, 0, start};
                }
                return {};
            }
            if (value.value.empty()) return {};
            auto parsed = parse_decimal(value.value);
            parsed.issue_offset = start;
            return parsed;
        }
        return {ArithmeticStatus::Syntax, 0, at_};
    }

    std::string_view text_;
    const ShellState& state_;
    std::size_t at_ = 0;
};

}  // namespace

ArithmeticResult parse_decimal(std::string_view text) {
    bool negative = false;
    std::size_t at = 0;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        negative = text[0] == '-';
        at = 1;
    }
    auto result = magnitude(text.substr(at), negative);
    result.issue_offset += at;
    return result;
}

ArithmeticResult evaluate_arithmetic(std::string_view expression,
                                      const ShellState& state) {
    Expression parser{expression, state};
    return parser.run();
}

NumericTestResult numeric_test(NumericOperator operation,
                                std::string_view left,
                                std::string_view right) {
    const auto a = parse_decimal(left);
    if (!a.ok()) return {a.status, false};
    const auto b = parse_decimal(right);
    if (!b.ok()) return {b.status, false};
    bool value = false;
    switch (operation) {
        case NumericOperator::Eq: value = a.value == b.value; break;
        case NumericOperator::Ne: value = a.value != b.value; break;
        case NumericOperator::Lt: value = a.value < b.value; break;
        case NumericOperator::Le: value = a.value <= b.value; break;
        case NumericOperator::Gt: value = a.value > b.value; break;
        case NumericOperator::Ge: value = a.value >= b.value; break;
    }
    return {ArithmeticStatus::Ok, value};
}

}  // namespace mm::shell
