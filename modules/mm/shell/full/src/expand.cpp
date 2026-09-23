// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :expand;
import mm.shell;

namespace mm::shell::full {
namespace {

[[nodiscard]] bool is_hex(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

[[nodiscard]] unsigned int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

[[nodiscard]] bool name_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Finds the end of a $( ... ) or $(( ... )) form, counting nested parentheses.
// Returns npos when the form is not closed.
[[nodiscard]] std::size_t closing_paren(std::string_view text,
                                        std::size_t open) {
    std::size_t depth = 0;
    for (std::size_t at = open; at < text.size(); ++at) {
        if (text[at] == '(') {
            ++depth;
            continue;
        }
        if (text[at] != ')') continue;
        --depth;
        if (depth == 0) return at;
    }
    return std::string_view::npos;
}

[[nodiscard]] std::size_t closing_brace(std::string_view text,
                                        std::size_t open) {
    std::size_t depth = 0;
    for (std::size_t at = open; at < text.size(); ++at) {
        if (text[at] == '{') {
            ++depth;
            continue;
        }
        if (text[at] != '}') continue;
        --depth;
        if (depth == 0) return at;
    }
    return std::string_view::npos;
}

// One byte of a bare $name, $1, $?, $#, $$, $@ or $* reference.
[[nodiscard]] std::size_t bare_reference(std::string_view text,
                                         std::size_t at) {
    if (at >= text.size()) return 0;
    const auto c = text[at];
    if (c == '?' || c == '#' || c == '$' || c == '@' || c == '*') return 1;
    if (c >= '0' && c <= '9') {
        std::size_t length = 0;
        while (at + length < text.size() && text[at + length] >= '0' &&
               text[at + length] <= '9') {
            ++length;
        }
        return length;
    }
    if (!name_char(c)) return 0;
    std::size_t length = 0;
    while (at + length < text.size() && name_char(text[at + length])) {
        ++length;
    }
    return length;
}

void append_literal(std::vector<HereSegment>& segments,
                    std::string_view text) {
    if (text.empty()) return;
    if (!segments.empty() &&
        segments.back().kind == HereSegmentKind::Literal) {
        segments.back().text.append(text);
        return;
    }
    segments.push_back({HereSegmentKind::Literal, std::string{text}});
}

}  // namespace

TrimResult trim_parameter(std::string_view value,
                          std::string_view pattern, TrimMode mode) {
    std::vector<PatternByte> bytes;
    bytes.reserve(pattern.size());
    for (const char c : pattern) bytes.push_back({c, false});
    const bool prefix = mode == TrimMode::ShortestPrefix ||
                        mode == TrimMode::LongestPrefix;
    const bool longest = mode == TrimMode::LongestPrefix ||
                         mode == TrimMode::LongestSuffix;
    for (std::size_t attempt = 0; attempt <= value.size(); ++attempt) {
        const auto length = longest ? value.size() - attempt : attempt;
        const auto candidate = prefix
            ? value.substr(0, length)
            : value.substr(value.size() - length);
        const auto matched = match_case_pattern(bytes, candidate);
        if (matched.status != Status::Ok) return {matched.status, {}};
        if (!matched.matched) continue;
        const auto retained = prefix
            ? value.substr(length)
            : value.substr(0, value.size() - length);
        return {Status::Ok, std::string(retained)};
    }
    return {Status::Ok, std::string(value)};
}

ArithmeticResult evaluate_full_arithmetic(
    std::string_view expression, const ShellState& state) {
    std::string decimal;
    decimal.reserve(expression.size());
    std::vector<std::size_t> offsets;
    offsets.reserve(expression.size());
    for (std::size_t at = 0; at < expression.size();) {
        const bool boundary = at == 0 ||
            !name_char(expression[at - 1]);
        if (boundary && at + 1 < expression.size() &&
            expression[at] == '0' &&
            (expression[at + 1] == 'x' ||
             expression[at + 1] == 'X')) {
            const auto start = at;
            at += 2;
            if (at == expression.size() || !is_hex(expression[at])) {
                return {ArithmeticStatus::Syntax, 0, start};
            }
            std::uint64_t value = 0;
            constexpr auto maximum = static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max());
            while (at < expression.size() && is_hex(expression[at])) {
                const auto digit = hex_digit(expression[at]);
                if (value > (maximum - digit) / 16) {
                    return {ArithmeticStatus::Range, 0, start};
                }
                value = value * 16 + digit;
                ++at;
            }
            const auto digits = std::to_string(value);
            decimal += digits;
            for (std::size_t i = 0; i < digits.size(); ++i) {
                offsets.push_back(start);
            }
        } else {
            decimal.push_back(expression[at]);
            offsets.push_back(at++);
        }
    }
    auto result = evaluate_arithmetic(decimal, state);
    if (!result.ok()) {
        result.issue_offset = result.issue_offset < offsets.size()
            ? offsets[result.issue_offset] : expression.size();
    }
    return result;
}

namespace {

// Resolves one parameter spelling to its bytes. A here-document never splits
// fields, so $@ and $* both join with a single space.
[[nodiscard]] Status resolve_scalar(std::string_view spelling,
                                    const ShellState& state,
                                    std::string& out, unsigned int depth);

[[nodiscard]] Status expand_operand(std::string_view operand,
                                    const ShellState& state,
                                    std::string& out, unsigned int depth) {
    for (std::size_t at = 0; at < operand.size();) {
        if (operand[at] != '$') {
            out.push_back(operand[at++]);
            continue;
        }
        std::size_t length = 0;
        if (at + 1 < operand.size() && operand[at + 1] == '{') {
            const auto close = closing_brace(operand, at + 1);
            if (close == std::string_view::npos) return Status::BadArgument;
            length = close - at + 1;
        } else {
            length = 1 + bare_reference(operand, at + 1);
            if (length == 1) {
                out.push_back(operand[at++]);
                continue;
            }
        }
        const auto status = resolve_scalar(operand.substr(at, length), state,
                                           out, depth + 1);
        if (status != Status::Ok) return status;
        at += length;
    }
    return Status::Ok;
}

Status resolve_scalar(std::string_view spelling, const ShellState& state,
                      std::string& out, unsigned int depth) {
    if (depth > 16) return Status::Overflow;
    const auto parsed = parse_parameter(spelling);
    if (parsed.status != ParameterStatus::Ok) return Status::BadArgument;
    // An assignment operand would mutate the state a here-document only reads.
    if (parsed.parameter.operation == ParameterOperator::Assign) {
        return Status::Unsupported;
    }
    const auto resolved = resolve_parameter(parsed.parameter, state);
    if (resolved.status != Status::Ok) return resolved.status;
    switch (resolved.kind) {
        case ParameterValueKind::Text:
            out.append(resolved.text);
            return Status::Ok;
        case ParameterValueKind::Number:
            out.append(std::to_string(resolved.number));
            return Status::Ok;
        case ParameterValueKind::Empty:
            return Status::Ok;
        case ParameterValueKind::Arguments: {
            for (std::size_t i = 1; i <= resolved.argument_count; ++i) {
                if (i != 1) out.push_back(' ');
                out.append(state.positional(i).value);
            }
            return Status::Ok;
        }
        case ParameterValueKind::Operand:
        case ParameterValueKind::AssignOperand:
            return expand_operand(resolved.text, state, out, depth);
    }
    return Status::Unsupported;
}

}  // namespace

HereBody expand_here_document(std::string_view body, bool expand,
                              const ShellState& state) {
    HereBody result;
    result.expanded = expand;
    if (!expand) {
        append_literal(result.segments, body);
        return result;
    }
    std::string literal;
    for (std::size_t at = 0; at < body.size();) {
        const auto c = body[at];
        if (c == '\\' && at + 1 < body.size()) {
            const auto next = body[at + 1];
            // Inside a here-document a backslash escapes only these, and is
            // otherwise a literal byte.
            if (next == '$' || next == '`' || next == '\\' ||
                next == '\n') {
                if (next != '\n') literal.push_back(next);
                at += 2;
                continue;
            }
            literal.push_back(c);
            ++at;
            continue;
        }
        if (c == '`') {
            result.status = Status::Unsupported;
            result.issue = at;
            return result;
        }
        if (c != '$') {
            literal.push_back(c);
            ++at;
            continue;
        }
        if (at + 2 < body.size() && body[at + 1] == '(' &&
            body[at + 2] == '(') {
            // Match the inner parenthesis, so the pair that closes the
            // arithmetic form is identified rather than the outer one alone.
            const auto inner = closing_paren(body, at + 2);
            if (inner == std::string_view::npos ||
                inner + 1 >= body.size() || body[inner + 1] != ')') {
                result.status = Status::BadArgument;
                result.issue = at;
                return result;
            }
            const auto expression = body.substr(at + 3,
                                                inner - (at + 3));
            const auto value = evaluate_full_arithmetic(expression, state);
            if (!value.ok()) {
                result.status = Status::BadArgument;
                result.issue = at;
                return result;
            }
            literal.append(std::to_string(value.value));
            at = inner + 2;
            continue;
        }
        if (at + 1 < body.size() && body[at + 1] == '(') {
            const auto close = closing_paren(body, at + 1);
            if (close == std::string_view::npos) {
                result.status = Status::BadArgument;
                result.issue = at;
                return result;
            }
            append_literal(result.segments, literal);
            literal.clear();
            result.segments.push_back(
                {HereSegmentKind::Substitution,
                 std::string{body.substr(at + 2, close - (at + 2))}});
            at = close + 1;
            continue;
        }
        std::size_t length = 0;
        if (at + 1 < body.size() && body[at + 1] == '{') {
            const auto close = closing_brace(body, at + 1);
            if (close == std::string_view::npos) {
                result.status = Status::BadArgument;
                result.issue = at;
                return result;
            }
            length = close - at + 1;
        } else {
            length = 1 + bare_reference(body, at + 1);
            if (length == 1) {
                literal.push_back(c);
                ++at;
                continue;
            }
        }
        const auto status = resolve_scalar(body.substr(at, length), state,
                                           literal, 0);
        if (status != Status::Ok) {
            result.status = status;
            result.issue = at;
            return result;
        }
        at += length;
    }
    append_literal(result.segments, literal);
    return result;
}

}  // namespace mm::shell::full
