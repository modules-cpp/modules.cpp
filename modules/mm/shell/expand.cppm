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
};

struct WordExpansionResult {
    Status status = Status::Ok;
    OverflowInfo overflow;
    std::size_t piece_count = 0;
    std::size_t generated_bytes = 0;
    ArithmeticStatus arithmetic_error = ArithmeticStatus::Ok;
};

// Scratch pieces and generated_text may be unspecified after failure, but
// the published FieldView and its field storage remain unchanged.
[[nodiscard]] WordExpansionResult expand_word(
    SourceView source, std::span<const WordFragment> fragments,
    const ShellState& state, WordExpansionStorage storage,
    FieldView& out);

}  // namespace mm::shell
