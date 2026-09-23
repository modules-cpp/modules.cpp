// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

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
    // Command-substitution results the evaluator captured before this word was
    // expanded, in fragment order. An empty span makes a substitution
    // fragment Status::Unsupported, which is what a caller that cannot run a
    // nested list wants.
    std::span<const std::string_view> substitutions;
};

struct WordExpansionResult {
    Status status = Status::Ok;
    OverflowInfo overflow;
    std::size_t piece_count = 0;
    std::size_t generated_bytes = 0;
    // How many entries of storage.substitutions this word consumed, so the
    // caller can advance to the next word's results.
    std::size_t substitutions_used = 0;
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
