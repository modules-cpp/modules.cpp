// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :source;
import :status;
import :word;
import mm.parse;

namespace mm::shell {
namespace {

[[nodiscard]] SourceLocation location(std::string_view text,
                                       std::size_t at) {
    SourceLocation result{.offset = at};
    for (std::size_t i = 0; i < at && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++result.line;
            result.column = 1;
        } else if (text[i] != '\r') {
            ++result.column;
        }
    }
    return result;
}

struct Writer {
    std::span<WordFragment> output;
    std::string_view text;
    std::size_t count = 0;
    bool has_unquoted_glob = false;
    WordFragment* selected = nullptr;
    std::size_t selected_index = 0;

    void add(FragmentKind kind, std::size_t offset,
             std::size_t length, bool quoted) {
        if (kind == FragmentKind::Literal && !quoted &&
            text.substr(offset, length).find_first_of("*?[]") !=
                std::string_view::npos) {
            has_unquoted_glob = true;
        }
        if (count < output.size()) {
            output[count] = {kind, {offset, length}, quoted};
        }
        if (selected != nullptr && count == selected_index) {
            *selected = {kind, {offset, length}, quoted};
        }
        ++count;
    }
};

// Free-function adapters so mm.parse::Cursor can drive the Writer through
// the Sink function-pointer seam. The embedded dialect's TokenKind and
// FragmentKind are aliases for mm::parse::TokenKind and
// mm::parse::FragmentKind, so no translation is needed.
static void sink_emit_fragment(void* ctx, mm::parse::FragmentKind kind,
                                mm::parse::SourceSpan span, bool quoted) {
    auto* writer = static_cast<Writer*>(ctx);
    writer->add(kind, span.offset, span.length, quoted);
}

static void sink_emit_token(void* ctx, mm::parse::TokenKind kind,
                             mm::parse::SourceSpan span, bool quoted) {
    (void)ctx;
    (void)kind;
    (void)span;
    (void)quoted;
}

// The embedded dialect drives mm.parse::Cursor through the Sink seam.
// The Writer is the Sink context; fragments are the payload.
[[nodiscard]] ScanOutcome scan_once(SourceView source,
                                     std::size_t offset,
                                     Writer& writer) {
    const auto text = source.text();
    mm::parse::Cursor cursor{text, offset, mm::parse::Dialect::Embedded};
    mm::parse::Sink sink{
        &writer,
        sink_emit_token,
        sink_emit_fragment,
        false,
    };
    const auto outcome = cursor.scan(sink);
    ScanOutcome result;
    result.token.kind = outcome.kind;
    result.token.source = outcome.span;
    result.token.next_offset = outcome.next_offset;
    result.token.fragments_required = writer.count;
    result.token.has_unquoted_glob = writer.has_unquoted_glob;
    if (!outcome.complete) {
        // An unclosed construct could still be finished by more text.
        result.status = outcome.message.starts_with("unclosed") ||
                                outcome.message == "trailing escape"
                            ? ScanStatus::Incomplete
                            : ScanStatus::Malformed;
        result.issue = location(text, cursor.offset());
    }
    return result;
}

}  // namespace

ScanOutcome scan_embedded(SourceView source, std::size_t offset,
                          std::span<WordFragment> fragments) {
    if (offset > source.size()) {
        return {.status = ScanStatus::Malformed,
                .issue = location(source.text(), source.size())};
    }
    Writer count{{}, source.text()};
    const auto measured = scan_once(source, offset, count);
    if (measured.status != ScanStatus::Complete || fragments.empty() ||
        count.count == 0) return measured;
    if (fragments.size() < count.count) {
        return {.status = ScanStatus::Overflow,
                .token = measured.token,
                .overflow = {StorageClass::WordFragments, count.count}};
    }
    Writer build{fragments, source.text()};
    return scan_once(source, offset, build);
}

ScanOutcome scan_embedded_fragment(SourceView source, std::size_t offset,
                                   std::size_t index,
                                   WordFragment& fragment) {
    Writer count{{}, source.text()};
    const auto measured = scan_once(source, offset, count);
    if (measured.status != ScanStatus::Complete) return measured;
    if (index >= count.count) {
        return {.status = ScanStatus::Malformed,
                .token = measured.token,
                .issue = location(source.text(), offset)};
    }
    Writer selected{{}, source.text(), 0, false, &fragment, index};
    return scan_once(source, offset, selected);
}

}  // namespace mm::shell
