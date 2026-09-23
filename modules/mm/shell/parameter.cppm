// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module mm.shell:parameter;

import :state;
import :status;
import :fields;

export namespace mm::shell {

enum class ParameterKind {
    Name,
    Positional,
    Count,
    LastStatus,
    ShellId,
    Star,
    At,
};

enum class ParameterOperator { None, Default, Alternate, Assign };
enum class ParameterStatus { Ok, Syntax, Range };

struct ParameterSpec {
    ParameterKind kind = ParameterKind::Name;
    ParameterOperator operation = ParameterOperator::None;
    std::string_view name;
    std::uint32_t index = 0;
    std::string_view operand;
};

struct ParameterParseResult {
    ParameterStatus status = ParameterStatus::Ok;
    ParameterSpec parameter;
};

enum class ParameterValueKind {
    Text,
    Number,
    Arguments,
    Operand,
    AssignOperand,
    Empty,
};

struct ParameterResolution {
    Status status = Status::Ok;
    ParameterValueKind kind = ParameterValueKind::Empty;
    std::string_view text;
    std::uint64_t number = 0;
    std::size_t argument_count = 0;
    ParameterKind arguments_kind = ParameterKind::At;
};

struct ParameterPiecesResult {
    Status status = Status::Ok;
    std::size_t piece_count = 0;
    std::size_t text_bytes = 0;
    OverflowInfo overflow;
};

[[nodiscard]] ParameterParseResult parse_parameter(
    std::string_view spelling);
[[nodiscard]] ParameterResolution resolve_parameter(
    const ParameterSpec& parameter, const ShellState& state);

// Operand and AssignOperand require recursive expansion and are rejected
// here. Other values become pieces for the shared field splitter.
[[nodiscard]] ParameterPiecesResult materialize_parameter(
    const ParameterResolution& value, const ShellState& state,
    bool quoted, std::span<FieldPiece> pieces,
    std::span<char> number_text);

}  // namespace mm::shell
