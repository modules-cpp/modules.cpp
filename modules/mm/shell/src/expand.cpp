// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module mm.shell;

import :arithmetic;
import :expand;
import :fields;
import :parameter;
import :source;
import :state;
import :status;
import :word;

namespace mm::shell {
namespace {

[[nodiscard]] std::uint64_t absolute(std::int64_t value) {
    if (value >= 0) return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

[[nodiscard]] std::size_t decimal_length(std::int64_t value) {
    auto number = absolute(value);
    std::size_t count = value < 0 ? 1 : 0;
    do {
        ++count;
        number /= 10;
    } while (number != 0);
    return count;
}

void write_decimal(std::int64_t value, std::span<char> output) {
    auto number = absolute(value);
    for (std::size_t i = output.size(); i > (value < 0 ? 1U : 0U); --i) {
        output[i - 1] = static_cast<char>('0' + number % 10);
        number /= 10;
    }
    if (value < 0) output[0] = '-';
}

[[nodiscard]] bool valid_span(SourceView source, SourceSpan span) {
    return span.offset <= source.size() &&
           span.length <= source.size() - span.offset;
}

[[nodiscard]] std::size_t parameter_end(std::string_view text,
                                        std::size_t start) {
    std::size_t depth = 0;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (text[i] == '$' && i + 1 < text.size() &&
            text[i + 1] == '{') {
            ++depth;
            ++i;
        } else if (text[i] == '}' && depth != 0 && --depth == 0) {
            return i + 1;
        }
    }
    return text.size() + 1;
}

[[nodiscard]] bool operand_may_assign(std::string_view text,
                                       unsigned int depth) {
    if (depth > 16) return false;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != '$' || text[i + 1] != '{') continue;
        const auto end = parameter_end(text, i);
        if (end > text.size()) return false;
        const auto parsed = parse_parameter(text.substr(i, end - i));
        if (parsed.status == ParameterStatus::Ok &&
            (parsed.parameter.operation == ParameterOperator::Assign ||
             operand_may_assign(parsed.parameter.operand, depth + 1))) {
            return true;
        }
        i = end - 1;
    }
    return false;
}

[[nodiscard]] bool needs_shadow(
    SourceView source, std::span<const WordFragment> fragments) {
    for (const auto& fragment : fragments) {
        if (fragment.kind != FragmentKind::Parameter ||
            !valid_span(source, fragment.source)) continue;
        const auto parsed = parse_parameter(source.slice(fragment.source));
        if (parsed.status == ParameterStatus::Ok &&
            (parsed.parameter.operation == ParameterOperator::Assign ||
             operand_may_assign(parsed.parameter.operand, 0))) {
            return true;
        }
    }
    return false;
}

struct OperandResult {
    Status status = Status::Ok;
    OverflowInfo overflow;
};

[[nodiscard]] std::string_view text_view(std::span<const char> text,
                                         std::size_t begin,
                                         std::size_t end) {
    if (end == begin) return {};
    return {text.data() + begin, end - begin};
}

[[nodiscard]] OperandResult append_text(std::string_view text,
                                        std::span<char> output,
                                        std::size_t& used) {
    if (text.size() > output.size() - used) {
        return {Status::Overflow,
                {StorageClass::ExpansionScratch, used + text.size()}};
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        output[used + i] = text[i];
    }
    used += text.size();
    return {};
}

[[nodiscard]] OperandResult append_operand(
    std::string_view text, ShellState& state, bool transactional,
    std::span<char> output, std::size_t& used, unsigned int depth) {
    if (depth > 16) {
        return {Status::Overflow,
                {StorageClass::ExpansionPieces, depth}};
    }
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\'' || c == '"' || c == '\\' || c == '`') {
            return {Status::Unsupported};
        }
        if (c != '$') {
            const auto next = text.find_first_of("$'\"\\`", i);
            const auto end = next == std::string_view::npos ?
                text.size() : next;
            const auto copied = append_text(text.substr(i, end - i),
                                            output, used);
            if (copied.status != Status::Ok) return copied;
            i = end;
            continue;
        }
        std::size_t end = i + 1;
        if (end == text.size()) {
            return append_text(text.substr(i, 1), output, used);
        }
        if (text[end] == '{') {
            end = parameter_end(text, i);
            if (end > text.size()) return {Status::BadArgument};
        } else if ((text[end] >= 'A' && text[end] <= 'Z') ||
                   (text[end] >= 'a' && text[end] <= 'z') ||
                   text[end] == '_') {
            ++end;
            while (end < text.size() &&
                   ((text[end] >= 'A' && text[end] <= 'Z') ||
                    (text[end] >= 'a' && text[end] <= 'z') ||
                    (text[end] >= '0' && text[end] <= '9') ||
                    text[end] == '_')) ++end;
        } else if ((text[end] >= '0' && text[end] <= '9') ||
                   text[end] == '#' || text[end] == '?' ||
                   text[end] == '$' || text[end] == '*' ||
                   text[end] == '@') {
            ++end;
        } else {
            return {Status::Unsupported};
        }
        const auto parsed = parse_parameter(text.substr(i, end - i));
        if (parsed.status != ParameterStatus::Ok) {
            return {Status::BadArgument};
        }
        const auto selected = resolve_parameter(parsed.parameter, state);
        if (selected.status != Status::Ok) return {selected.status};
        if (selected.kind == ParameterValueKind::Arguments) {
            return {Status::Unsupported};
        }
        if (selected.kind == ParameterValueKind::Operand ||
            selected.kind == ParameterValueKind::AssignOperand) {
            const auto value_start = used;
            const auto expanded = append_operand(
                selected.text, state, transactional, output, used,
                depth + 1);
            if (expanded.status != Status::Ok) return expanded;
            if (selected.kind == ParameterValueKind::AssignOperand) {
                if (!transactional) return {Status::BadArgument};
                const auto value = text_view(output, value_start, used);
                const auto assigned = state.assign(
                    parsed.parameter.name, value);
                if (!assigned.ok()) {
                    return {assigned.status, assigned.overflow};
                }
            }
        } else if (selected.kind == ParameterValueKind::Number) {
            auto number = selected.number;
            std::size_t bytes = 0;
            do { ++bytes; number /= 10; } while (number != 0);
            if (bytes > output.size() - used) {
                return {Status::Overflow,
                        {StorageClass::ExpansionScratch, used + bytes}};
            }
            number = selected.number;
            for (std::size_t j = bytes; j > 0; --j) {
                output[used + j - 1] =
                    static_cast<char>('0' + number % 10);
                number /= 10;
            }
            used += bytes;
        } else {
            const auto copied = append_text(selected.text, output, used);
            if (copied.status != Status::Ok) return copied;
        }
        i = end;
    }
    return {};
}

}  // namespace

WordExpansionResult expand_word(
    SourceView source, std::span<const WordFragment> fragments,
    ShellState& state, WordExpansionStorage storage,
    FieldView& out) {
    ShellState shadow;
    const bool transactional = needs_shadow(source, fragments);
    if (transactional) {
        const auto fork = state.fork_variables(
            storage.shadow_variables, storage.shadow_variable_text,
            shadow);
        if (!fork.ok()) return {fork.status, fork.overflow};
    }
    ShellState& working = transactional ? shadow : state;
    std::size_t piece_count = 0;
    std::size_t generated = 0;
    for (const auto& fragment : fragments) {
        if (!valid_span(source, fragment.source)) {
            return {Status::BadArgument};
        }
        const auto spelling = source.slice(fragment.source);
        if (fragment.kind == FragmentKind::Literal ||
            fragment.kind == FragmentKind::SingleQuoted ||
            fragment.kind == FragmentKind::DoubleQuoted ||
            fragment.kind == FragmentKind::Escaped) {
            if (piece_count == storage.pieces.size()) {
                return {Status::Overflow,
                        {StorageClass::ExpansionPieces, piece_count + 1}};
            }
            storage.pieces[piece_count++] = {
                fragment.quoted ? FieldPieceKind::Quoted
                                : FieldPieceKind::Literal,
                spelling};
            continue;
        }
        if (fragment.kind == FragmentKind::CommandSubstitution) {
            return {Status::Unsupported};
        }
        if (fragment.kind == FragmentKind::Arithmetic) {
            if (spelling.size() < 5 ||
                !spelling.starts_with("$((") ||
                !spelling.ends_with("))")) {
                return {Status::BadArgument};
            }
            const auto expression = spelling.substr(
                3, spelling.size() - 5);
            const auto result = evaluate_arithmetic(expression, working);
            if (!result.ok()) {
                WordExpansionResult failure{Status::BadArgument};
                failure.arithmetic_error = result.status;
                return failure;
            }
            const auto bytes = decimal_length(result.value);
            if (bytes > storage.generated_text.size() - generated) {
                return {Status::Overflow,
                        {StorageClass::ExpansionScratch,
                         generated + bytes}};
            }
            if (piece_count == storage.pieces.size()) {
                return {Status::Overflow,
                        {StorageClass::ExpansionPieces, piece_count + 1}};
            }
            auto output = storage.generated_text.subspan(generated, bytes);
            write_decimal(result.value, output);
            storage.pieces[piece_count++] = {
                fragment.quoted ? FieldPieceKind::Quoted
                                : FieldPieceKind::Split,
                {output.data(), output.size()}};
            generated += bytes;
            continue;
        }
        const auto parsed = parse_parameter(spelling);
        if (parsed.status != ParameterStatus::Ok) {
            return {Status::BadArgument};
        }
        const auto selected = resolve_parameter(parsed.parameter, working);
        if (selected.status != Status::Ok) return {selected.status};
        if (selected.kind == ParameterValueKind::Operand ||
            selected.kind == ParameterValueKind::AssignOperand) {
            if (piece_count == storage.pieces.size()) {
                return {Status::Overflow,
                        {StorageClass::ExpansionPieces, piece_count + 1}};
            }
            const auto begin = generated;
            const auto expanded = append_operand(
                selected.text, working, transactional,
                storage.generated_text, generated, 0);
            if (expanded.status != Status::Ok) {
                return {expanded.status, expanded.overflow};
            }
            const auto value = text_view(
                storage.generated_text, begin, generated);
            if (selected.kind == ParameterValueKind::AssignOperand) {
                if (!transactional) return {Status::BadArgument};
                const auto assigned = shadow.assign(
                    parsed.parameter.name, value);
                if (!assigned.ok()) {
                    return {assigned.status, assigned.overflow};
                }
            }
            storage.pieces[piece_count++] = {
                fragment.quoted ? FieldPieceKind::Quoted
                                : FieldPieceKind::Split,
                value};
            continue;
        }
        const auto result = materialize_parameter(
            selected, working, fragment.quoted,
            storage.pieces.subspan(piece_count),
            storage.generated_text.subspan(generated));
        if (result.status != Status::Ok) {
            return {result.status, result.overflow,
                    piece_count, generated};
        }
        generated += result.text_bytes;
        if (transactional) {
            for (std::size_t i = 0; i < result.piece_count; ++i) {
                auto& piece = storage.pieces[piece_count + i];
                if (piece.kind == FieldPieceKind::Boundary ||
                    piece.text.empty() ||
                    (selected.kind == ParameterValueKind::Number)) {
                    continue;
                }
                const auto bytes = piece.text.size();
                if (bytes > storage.generated_text.size() - generated) {
                    return {Status::Overflow,
                            {StorageClass::ExpansionScratch,
                             generated + bytes}};
                }
                auto output = storage.generated_text.subspan(
                    generated, bytes);
                for (std::size_t j = 0; j < bytes; ++j) {
                    output[j] = piece.text[j];
                }
                piece.text = {output.data(), bytes};
                generated += bytes;
            }
        }
        piece_count += result.piece_count;
    }
    const auto split = split_fields(
        storage.pieces.first(piece_count), working.ifs(),
        storage.fields, out);
    if (split.status != Status::Ok) {
        return {split.status, split.overflow, piece_count, generated};
    }
    if (transactional) {
        const auto commit = state.commit_variables_from(shadow);
        if (!commit.ok()) {
            return {commit.status, commit.overflow, piece_count,
                    generated};
        }
    }
    return {split.status, split.overflow, piece_count, generated};
}

}  // namespace mm::shell
