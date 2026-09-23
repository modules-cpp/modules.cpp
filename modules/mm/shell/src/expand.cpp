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

}  // namespace

WordExpansionResult expand_word(
    SourceView source, std::span<const WordFragment> fragments,
    const ShellState& state, WordExpansionStorage storage,
    FieldView& out) {
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
            const auto result = evaluate_arithmetic(expression, state);
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
        const auto selected = resolve_parameter(parsed.parameter, state);
        if (selected.status != Status::Ok) return {selected.status};
        const auto result = materialize_parameter(
            selected, state, fragment.quoted,
            storage.pieces.subspan(piece_count),
            storage.generated_text.subspan(generated));
        if (result.status != Status::Ok) {
            return {result.status, result.overflow,
                    piece_count, generated};
        }
        piece_count += result.piece_count;
        generated += result.text_bytes;
    }
    const auto split = split_fields(
        storage.pieces.first(piece_count), state.ifs(), storage.fields, out);
    return {split.status, split.overflow, piece_count, generated};
}

}  // namespace mm::shell
