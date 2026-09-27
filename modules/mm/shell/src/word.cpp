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

// Dialect adapters: translate mm.parse superset enums to mm.shell enums.
[[nodiscard]] TokenKind to_shell_token(mm::parse::TokenKind kind) {
    using P = mm::parse::TokenKind;
    switch (kind) {
        case P::End: return TokenKind::End;
        case P::Newline: return TokenKind::Newline;
        case P::Word: return TokenKind::Word;
        case P::AndIf: return TokenKind::AndIf;
        case P::OrIf: return TokenKind::OrIf;
        case P::Semicolon: return TokenKind::Semicolon;
        case P::DoubleSemicolon: return TokenKind::DoubleSemicolon;
        case P::LeftParen: return TokenKind::LeftParen;
        case P::RightParen: return TokenKind::RightParen;
        case P::LeftBrace: return TokenKind::LeftBrace;
        case P::RightBrace: return TokenKind::RightBrace;
        case P::Bang: return TokenKind::Bang;
        case P::CaseBar: return TokenKind::CaseBar;
        default: return TokenKind::End;
    }
}

[[nodiscard]] FragmentKind to_shell_fragment(mm::parse::FragmentKind kind) {
    using P = mm::parse::FragmentKind;
    switch (kind) {
        case P::Literal: return FragmentKind::Literal;
        case P::SingleQuoted: return FragmentKind::SingleQuoted;
        case P::DoubleQuoted: return FragmentKind::DoubleQuoted;
        case P::Escaped: return FragmentKind::Escaped;
        case P::Parameter: return FragmentKind::Parameter;
        case P::Arithmetic: return FragmentKind::Arithmetic;
        case P::CommandSubstitution: return FragmentKind::CommandSubstitution;
        default: return FragmentKind::Literal;
    }
}

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
// the Sink function-pointer seam.
static void sink_emit_fragment(void* ctx, mm::parse::FragmentKind kind,
                                mm::parse::SourceSpan span, bool quoted) {
    auto* writer = static_cast<Writer*>(ctx);
    writer->add(to_shell_fragment(kind), span.offset, span.length, quoted);
}

static void sink_emit_token(void* ctx, mm::parse::TokenKind kind,
                             mm::parse::SourceSpan span, bool quoted) {
    // Token emission is recorded by the Cursor's ScanOutcome; the embedded
    // dialect's scan_once reads the outcome directly. The token callback is
    // a no-op for the embedded dialect: fragments are the payload.
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
    result.token.kind = to_shell_token(outcome.kind);
    result.token.source = {outcome.span.offset, outcome.span.length};
    result.token.next_offset = outcome.next_offset;
    result.token.fragments_required = static_cast<std::size_t>(outcome.fragment_count);
    result.token.has_unquoted_glob = writer.has_unquoted_glob;
    if (!outcome.complete) {
        result.status = ScanStatus::Malformed;
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
