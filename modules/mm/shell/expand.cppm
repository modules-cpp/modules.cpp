// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:expand;

import :arithmetic;
import :fields;
import :source;
import :state;
import :status;
import :word;

export namespace mm::shell {

struct WordExpansionStorage {
    std::span<FieldPiece> pieces;
    std::span<char> generated_text;
    FieldStorage fields;
    // Required when a parameter operand syntactically contains :=, even
    // when that branch is not selected at runtime. Must not alias state
    // storage, generated_text, or fields.
    std::span<VariableSlot> shadow_variables;
    std::span<char> shadow_variable_text;
};

struct WordExpansionResult {
    Status status = Status::Ok;
    OverflowInfo overflow;
    std::size_t piece_count = 0;
    std::size_t generated_bytes = 0;
    ArithmeticStatus arithmetic_error = ArithmeticStatus::Ok;
};

// Assignment operands use non-aliasing shadow variable storage. Scratch
// may be unspecified after failure; state and published fields are not.
[[nodiscard]] WordExpansionResult expand_word(
    SourceView source, std::span<const WordFragment> fragments,
    ShellState& state, WordExpansionStorage storage,
    FieldView& out);

// Assignment values, case selectors, and other single-value operands expand
// without field splitting, so out publishes one field or none. value_offset
// is an absolute source offset that drops an unquoted literal prefix such as
// NAME=; zero expands the whole word.
[[nodiscard]] WordExpansionResult expand_value(
    SourceView source, std::span<const WordFragment> fragments,
    std::size_t value_offset, ShellState& state,
    WordExpansionStorage storage, FieldView& out);

}  // namespace mm::shell
