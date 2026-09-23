// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:fields;

import :source;
import :status;

export namespace mm::shell {

enum class FieldPieceKind {
    Literal,
    Quoted,
    Split,
    Boundary,
};

struct FieldPiece {
    FieldPieceKind kind = FieldPieceKind::Literal;
    std::string_view text;
};

struct FieldStorage {
    std::span<char> text;
    std::span<SourceSpan> fields;
};

struct FieldView {
    std::string_view text;
    std::span<const SourceSpan> fields;

    [[nodiscard]] std::string_view field(std::size_t index) const;
};

struct FieldCounts {
    std::size_t text = 0;
    std::size_t fields = 0;
};

struct FieldOutcome {
    Status status = Status::Ok;
    FieldCounts required;
    OverflowInfo overflow;
};

// Boundary separates quoted $@ arguments. Quoted empty text creates a field;
// an empty Split piece does not. Piece text must not alias output storage.
// On failure, storage and out are unchanged.
[[nodiscard]] FieldOutcome split_fields(
    std::span<const FieldPiece> pieces, std::string_view ifs,
    FieldStorage storage, FieldView& out);

}  // namespace mm::shell
