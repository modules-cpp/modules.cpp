// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

module mm.shell;

import :parameter;
import :fields;
import :state;
import :status;

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

ParameterResolution resolve_parameter(const ParameterSpec& parameter,
                                       const ShellState& state) {
    ParameterResolution value;
    bool set = true;
    bool nonnull = true;
    switch (parameter.kind) {
        case ParameterKind::Name: {
            const auto found = state.lookup(parameter.name);
            set = found.found;
            nonnull = found.found && !found.value.empty();
            value.kind = ParameterValueKind::Text;
            value.text = found.value;
            break;
        }
        case ParameterKind::Positional: {
            const auto found = state.positional(parameter.index);
            set = found.found;
            nonnull = found.found && !found.value.empty();
            value.kind = ParameterValueKind::Text;
            value.text = found.value;
            break;
        }
        case ParameterKind::Count:
            value.kind = ParameterValueKind::Number;
            value.number = state.argument_count();
            break;
        case ParameterKind::LastStatus:
            value.kind = ParameterValueKind::Number;
            value.number = static_cast<unsigned int>(
                state.last_status) & 0xffU;
            break;
        case ParameterKind::ShellId:
            value.kind = ParameterValueKind::Number;
            value.number = state.shell_id;
            break;
        case ParameterKind::Star:
        case ParameterKind::At: {
            value.kind = ParameterValueKind::Arguments;
            value.arguments_kind = parameter.kind;
            value.argument_count = state.argument_count();
            nonnull = value.argument_count > 1;
            if (value.argument_count == 1) {
                nonnull = !state.positional(1).value.empty();
            }
            if (parameter.kind == ParameterKind::Star &&
                value.argument_count > 1) {
                nonnull = !state.ifs().empty();
                for (std::size_t i = 1; i <= value.argument_count; ++i) {
                    if (!state.positional(i).value.empty()) nonnull = true;
                }
            }
            break;
        }
    }
    switch (parameter.operation) {
        case ParameterOperator::None:
            if (!set && state.nounset &&
                value.kind != ParameterValueKind::Arguments) {
                value.status = Status::NotFound;
            } else if (!set) {
                value.kind = ParameterValueKind::Empty;
            }
            break;
        case ParameterOperator::Default:
            if (!nonnull) {
                value.kind = ParameterValueKind::Operand;
                value.text = parameter.operand;
            }
            break;
        case ParameterOperator::Alternate:
            if (nonnull) {
                value.kind = ParameterValueKind::Operand;
                value.text = parameter.operand;
            } else {
                value.kind = ParameterValueKind::Empty;
                value.text = {};
            }
            break;
        case ParameterOperator::Assign:
            if (parameter.kind != ParameterKind::Name) {
                value.status = Status::BadArgument;
            } else if (!nonnull) {
                value.kind = ParameterValueKind::AssignOperand;
                value.text = parameter.operand;
            }
            break;
    }
    return value;
}

ParameterPiecesResult materialize_parameter(
    const ParameterResolution& value, const ShellState& state,
    bool quoted, std::span<FieldPiece> pieces,
    std::span<char> number_text) {
    if (value.status != Status::Ok) return {value.status};
    if (value.kind == ParameterValueKind::Operand ||
        value.kind == ParameterValueKind::AssignOperand) {
        return {Status::Unsupported};
    }
    std::size_t required_pieces = 1;
    std::size_t required_bytes = 0;
    if (value.kind == ParameterValueKind::Arguments) {
        const auto count = value.argument_count;
        constexpr auto maximum = std::numeric_limits<std::size_t>::max();
        if (count > maximum / 2 + 1) {
            return {Status::Overflow, maximum, 0,
                    {StorageClass::ExpansionPieces, maximum}};
        }
        required_pieces = count == 0 ?
            (quoted && value.arguments_kind == ParameterKind::Star ? 1 : 0)
            : count * 2 - 1;
    } else if (value.kind == ParameterValueKind::Number) {
        auto number = value.number;
        do {
            ++required_bytes;
            number /= 10;
        } while (number != 0);
    }
    if (pieces.size() < required_pieces) {
        return {Status::Overflow, required_pieces, required_bytes,
                {StorageClass::ExpansionPieces, required_pieces}};
    }
    if (number_text.size() < required_bytes) {
        return {Status::Overflow, required_pieces, required_bytes,
                {StorageClass::ExpansionScratch, required_bytes}};
    }
    const auto kind = quoted ? FieldPieceKind::Quoted
                             : FieldPieceKind::Split;
    if (value.kind == ParameterValueKind::Text ||
        value.kind == ParameterValueKind::Empty) {
        pieces[0] = {kind, value.text};
    } else if (value.kind == ParameterValueKind::Number) {
        auto number = value.number;
        for (std::size_t i = required_bytes; i > 0; --i) {
            number_text[i - 1] = static_cast<char>('0' + number % 10);
            number /= 10;
        }
        pieces[0] = {kind, {number_text.data(), required_bytes}};
    } else if (value.kind == ParameterValueKind::Arguments &&
               required_pieces != 0) {
        if (value.argument_count == 0) {
            pieces[0] = {FieldPieceKind::Quoted, {}};
        } else {
            std::size_t at = 0;
            const auto ifs = state.ifs();
            for (std::size_t i = 1; i <= value.argument_count; ++i) {
                if (i != 1) {
                    if (value.arguments_kind == ParameterKind::At) {
                        pieces[at++] = {FieldPieceKind::Boundary, {}};
                    } else {
                        pieces[at++] = {
                            kind, ifs.empty() ? std::string_view{}
                                              : ifs.substr(0, 1)};
                    }
                }
                pieces[at++] = {kind, state.positional(i).value};
            }
        }
    }
    return {Status::Ok, required_pieces, required_bytes, {}};
}

}  // namespace mm::shell
