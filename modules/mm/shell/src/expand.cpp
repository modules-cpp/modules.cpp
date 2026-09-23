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

[[nodiscard]] bool plain_operand(std::string_view text) {
    for (const char c : text) {
        if (c == '$' || c == '\'' || c == '"' || c == '\\' ||
            c == '`') return false;
    }
    return true;
}

[[nodiscard]] bool needs_shadow(
    SourceView source, std::span<const WordFragment> fragments) {
    for (const auto& fragment : fragments) {
        if (fragment.kind != FragmentKind::Parameter ||
            !valid_span(source, fragment.source)) continue;
        const auto parsed = parse_parameter(source.slice(fragment.source));
        if (parsed.status == ParameterStatus::Ok &&
            parsed.parameter.operation == ParameterOperator::Assign) {
            return true;
        }
    }
    return false;
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
    const ShellState& working = transactional ? shadow : state;
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
            if (!plain_operand(selected.text)) {
                return {Status::Unsupported};
            }
            if (piece_count == storage.pieces.size()) {
                return {Status::Overflow,
                        {StorageClass::ExpansionPieces, piece_count + 1}};
            }
            if (selected.kind == ParameterValueKind::AssignOperand) {
                const auto assigned = shadow.assign(
                    parsed.parameter.name, selected.text);
                if (!assigned.ok()) {
                    return {assigned.status, assigned.overflow};
                }
            }
            storage.pieces[piece_count++] = {
                fragment.quoted ? FieldPieceKind::Quoted
                                : FieldPieceKind::Split,
                selected.text};
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
