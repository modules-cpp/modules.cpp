// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <limits>
#include <string_view>

module mm.shell;

import :parameter;

namespace mm::shell {
namespace {

[[nodiscard]] bool digit(char c) { return c >= '0' && c <= '9'; }

[[nodiscard]] bool name_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_rest(char c) {
    return name_first(c) || digit(c);
}

[[nodiscard]] ParameterParseResult reference(std::string_view name,
                                             bool braced) {
    ParameterSpec spec;
    if (name.empty()) return {ParameterStatus::Syntax, {}};
    if (name.size() == 1) {
        switch (name[0]) {
            case '#': spec.kind = ParameterKind::Count; return {{}, spec};
            case '?': spec.kind = ParameterKind::LastStatus;
                      return {{}, spec};
            case '$': spec.kind = ParameterKind::ShellId;
                      return {{}, spec};
            case '*': spec.kind = ParameterKind::Star; return {{}, spec};
            case '@': spec.kind = ParameterKind::At; return {{}, spec};
            default: break;
        }
    }
    if (digit(name[0])) {
        if (!braced && name.size() != 1) {
            return {ParameterStatus::Syntax, {}};
        }
        if (braced && name.size() > 1 && name[0] == '0') {
            return {ParameterStatus::Syntax, {}};
        }
        std::uint32_t index = 0;
        constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
        for (const char c : name) {
            if (!digit(c)) return {ParameterStatus::Syntax, {}};
            const auto next = static_cast<std::uint32_t>(c - '0');
            if (index > (maximum - next) / 10) {
                return {ParameterStatus::Range, {}};
            }
            index = index * 10 + next;
        }
        spec.kind = ParameterKind::Positional;
        spec.index = index;
        return {{}, spec};
    }
    if (!name_first(name[0])) return {ParameterStatus::Syntax, {}};
    for (const char c : name.substr(1)) {
        if (!name_rest(c)) return {ParameterStatus::Syntax, {}};
    }
    spec.kind = ParameterKind::Name;
    spec.name = name;
    return {{}, spec};
}

}  // namespace

ParameterParseResult parse_parameter(std::string_view spelling) {
    if (spelling.size() < 2 || spelling[0] != '$') {
        return {ParameterStatus::Syntax, {}};
    }
    if (spelling[1] != '{') {
        return reference(spelling.substr(1), false);
    }
    if (spelling.size() < 4 || spelling.back() != '}') {
        return {ParameterStatus::Syntax, {}};
    }
    const auto body = spelling.substr(2, spelling.size() - 3);
    std::size_t operator_at = body.size();
    std::size_t nested = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (i + 1 < body.size() && body[i] == '$' &&
            body[i + 1] == '{') {
            ++nested;
            ++i;
            continue;
        }
        if (body[i] == '}' && nested != 0) {
            --nested;
            continue;
        }
        if (nested == 0 && body[i] == ':') {
            operator_at = i;
            break;
        }
    }
    if (nested != 0) return {ParameterStatus::Syntax, {}};
    auto result = reference(body.substr(0, operator_at), true);
    if (result.status != ParameterStatus::Ok ||
        operator_at == body.size()) return result;
    if (operator_at + 1 == body.size()) {
        return {ParameterStatus::Syntax, {}};
    }
    switch (body[operator_at + 1]) {
        case '-': result.parameter.operation =
                      ParameterOperator::Default; break;
        case '+': result.parameter.operation =
                      ParameterOperator::Alternate; break;
        case '=': result.parameter.operation =
                      ParameterOperator::Assign; break;
        default: return {ParameterStatus::Syntax, {}};
    }
    if (result.parameter.operation == ParameterOperator::Assign &&
        result.parameter.kind != ParameterKind::Name) {
        return {ParameterStatus::Syntax, {}};
    }
    result.parameter.operand = body.substr(operator_at + 2);
    return result;
}

}  // namespace mm::shell
