// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :fields;
import :source;
import :status;

namespace mm::shell {
namespace {

[[nodiscard]] bool ifs_blank(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

[[nodiscard]] bool in_ifs(std::string_view ifs, char c) {
    return ifs.find(c) != std::string_view::npos;
}

class Writer {
public:
    Writer(std::string_view ifs, FieldStorage* storage)
        : ifs_(ifs), storage_(storage) {}

    [[nodiscard]] FieldCounts run(std::span<const FieldPiece> pieces) {
        for (const auto& piece : pieces) {
            switch (piece.kind) {
                case FieldPieceKind::Literal:
                    append(piece.text, false);
                    break;
                case FieldPieceKind::Quoted:
                    start();
                    append(piece.text, false);
                    break;
                case FieldPieceKind::Split:
                    append(piece.text, true);
                    break;
                case FieldPieceKind::Boundary:
                    close();
                    pending_whitespace_ = false;
                    break;
            }
        }
        close();
        return {text_count_, field_count_};
    }

private:
    void start() {
        if (active_) return;
        active_ = true;
        field_start_ = text_count_;
        pending_whitespace_ = false;
    }

    void close() {
        if (!active_) return;
        if (storage_ != nullptr) {
            storage_->fields[field_count_] = {
                field_start_, text_count_ - field_start_};
        }
        ++field_count_;
        active_ = false;
    }

    void byte(char c) {
        start();
        if (storage_ != nullptr) storage_->text[text_count_] = c;
        ++text_count_;
    }

    void append(std::string_view text, bool split) {
        for (const char c : text) {
            if (split && in_ifs(ifs_, c)) {
                if (ifs_blank(c)) {
                    const bool had_field = active_;
                    close();
                    if (had_field) pending_whitespace_ = true;
                } else {
                    if (active_) {
                        close();
                    } else if (!pending_whitespace_) {
                        start();
                        close();
                    }
                    pending_whitespace_ = false;
                }
            } else {
                byte(c);
            }
        }
    }

    std::string_view ifs_;
    FieldStorage* storage_;
    std::size_t text_count_ = 0;
    std::size_t field_count_ = 0;
    std::size_t field_start_ = 0;
    bool active_ = false;
    bool pending_whitespace_ = false;
};

}  // namespace

std::string_view FieldView::field(std::size_t index) const {
    if (index >= fields.size()) return {};
    const auto span = fields[index];
    if (span.length == 0) return {};
    return text.substr(span.offset, span.length);
}

FieldOutcome split_fields(std::span<const FieldPiece> pieces,
                          std::string_view ifs, FieldStorage storage,
                          FieldView& out) {
    Writer count{ifs, nullptr};
    const auto required = count.run(pieces);
    if (storage.text.size() < required.text) {
        return {Status::Overflow, required,
                {StorageClass::ExpandedFieldText, required.text}};
    }
    if (storage.fields.size() < required.fields) {
        return {Status::Overflow, required,
                {StorageClass::ExpandedFields, required.fields}};
    }
    Writer build{ifs, &storage};
    const auto built = build.run(pieces);
    out = {{storage.text.data(), built.text},
           storage.fields.first(built.fields)};
    return {Status::Ok, required, {}};
}

}  // namespace mm::shell
