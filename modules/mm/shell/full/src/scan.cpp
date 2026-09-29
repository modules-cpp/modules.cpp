// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module mm.shell.full;

import :scan;
import :syntax;
import mm.parse;

namespace mm::shell::full {
namespace {

struct PendingDocument {
    std::size_t operator_token = 0;
    std::string delimiter;
    bool expand = true;
};

// Sink context: the Scanner's output and state, driven by mm.parse::Cursor.
struct SinkContext {
    FullScript* output = nullptr;
    std::vector<PendingDocument>* pending;
    std::size_t* waiting;
    bool* wants_delimiter;
    bool* has_unquoted_glob;
};

// The status a scanner refusal carries, from the cursor's message: an
// unclosed construct could still be completed by more text, an excluded one
// is refused as unsupported, and anything else is malformed.
[[nodiscard]] ParseStatus refusal_status(std::string_view message) {
    if (message.starts_with("unclosed") || message == "trailing escape")
        return ParseStatus::Incomplete;
    if (message.ends_with("not supported") ||
        message == "excluded shell construct")
        return ParseStatus::Unsupported;
    return ParseStatus::Malformed;
}

// Translate mm.parse superset token kinds to full-profile kinds.
[[nodiscard]] TokenKind to_full_token(mm::parse::TokenKind kind) {
    using P = mm::parse::TokenKind;
    switch (kind) {
        case P::End: return TokenKind::End;
        case P::Newline: return TokenKind::Newline;
        case P::Word: return TokenKind::Word;
        case P::AndIf: return TokenKind::AndIf;
        case P::OrIf: return TokenKind::OrIf;
        case P::Semicolon: return TokenKind::Semicolon;
        case P::DoubleSemicolon: return TokenKind::DoubleSemicolon;
        case P::LeftParen: return TokenKind::OpenParen;
        case P::RightParen: return TokenKind::CloseParen;
        case P::LeftBrace: return TokenKind::OpenBrace;
        case P::RightBrace: return TokenKind::CloseBrace;
        case P::Pipe: return TokenKind::Pipe;
        case P::Input: return TokenKind::Input;
        case P::Output: return TokenKind::Output;
        case P::Append: return TokenKind::Append;
        case P::HereDocument: return TokenKind::HereDocument;
        case P::DuplicateInput: return TokenKind::DuplicateInput;
        case P::DuplicateOutput: return TokenKind::DuplicateOutput;
        case P::IoNumber: return TokenKind::IoNumber;
        default: return TokenKind::Word;
    }
}

// The Sink::emit_token callback: push the token into FullScript.tokens.
static void sink_emit_token(void* ctx, mm::parse::TokenKind kind,
                              mm::parse::SourceSpan span, bool quoted) {
    auto* sink = static_cast<SinkContext*>(ctx);
    TokenKind full_kind = to_full_token(kind);
    sink->output->tokens.push_back({full_kind, {span.offset, span.length},
                                    quoted});
}

// The Sink::emit_fragment callback: no fragments in the full dialect.
static void sink_emit_fragment(void* ctx, mm::parse::FragmentKind kind,
                                 mm::parse::SourceSpan span, bool quoted) {
    (void)ctx;
    (void)kind;
    (void)span;
    (void)quoted;
}

struct Scanner {
    FullScript& output;
    std::string_view source;
    std::vector<PendingDocument> pending;
    std::size_t at = 0;
    std::size_t waiting = 0;
    bool wants_delimiter = false;
    Diagnostic diagnostic;

    void fail(ParseStatus status, std::size_t offset,
              std::string_view message) {
        if (diagnostic.status != ParseStatus::Complete) return;
        diagnostic = {status, offset, std::string(message)};
    }

    // Handle a here-document delimiter: extract the delimiter from the
    // token text, store it in pending, and clear wants_delimiter.
    void handle_heredoc_delimiter(std::size_t offset, std::size_t length) {
        auto* sink = &sink_ctx_;
        const auto spelling = source.substr(offset, length);
        std::string delimiter;
        delimiter.reserve(spelling.size());
        char in_quote = 0;
        bool quoted_delimiter = false;
        for (std::size_t i = 0; i < spelling.size(); ++i) {
            const char ch = spelling[i];
            if (in_quote != 0 && ch == in_quote) {
                in_quote = 0;
                quoted_delimiter = true;
            } else if (in_quote == 0 &&
                       (ch == '\'' || ch == '"')) {
                in_quote = ch;
                quoted_delimiter = true;
            } else if (ch == '\\' && i + 1 < spelling.size()) {
                delimiter.push_back(spelling[++i]);
                quoted_delimiter = true;
            } else {
                delimiter.push_back(ch);
            }
        }
        if (delimiter.empty()) {
            fail(ParseStatus::Malformed, offset,
                 "empty here-document delimiter");
            return;
        }
        pending.push_back({*sink->waiting, std::move(delimiter),
                           !quoted_delimiter});
        *sink->wants_delimiter = false;
    }

    // Collect here-document bodies: read lines until the delimiter is found.
    void documents() {
        for (auto& item : pending) {
            const auto body_start = at;
            bool found = false;
            while (at < source.size()) {
                const auto line_start = at;
                const auto end = source.find('\n', at);
                const auto line_end = end == std::string_view::npos
                    ? source.size() : end;
                auto line = source.substr(line_start,
                                          line_end - line_start);
                if (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }
                at = end == std::string_view::npos
                    ? source.size() : end + 1;
                if (line == item.delimiter) {
                    output.documents.push_back({
                        item.operator_token,
                        {body_start, line_start - body_start},
                        std::move(item.delimiter), item.expand});
                    found = true;
                    break;
                }
            }
            if (!found) {
                fail(ParseStatus::Incomplete, body_start,
                     "unclosed here-document");
                return;
            }
        }
        pending.clear();
    }

    SinkContext sink_ctx_;
    mm::parse::Sink sink_;

    void run() {
        sink_ctx_ = {&output, &pending, &waiting, &wants_delimiter,
                      nullptr};
        sink_ = {&sink_ctx_, sink_emit_token, sink_emit_fragment, false};
        mm::parse::Cursor cursor{source, 0, mm::parse::Dialect::Full};

        while (!cursor.at_end() &&
               diagnostic.status == ParseStatus::Complete) {
            const auto outcome = cursor.scan(sink_);
            at = outcome.next_offset;

            if (outcome.kind == mm::parse::TokenKind::Newline) {
                // The cursor leaves newlines to the dialect: the full
                // grammar needs them as tokens, so emit one here.
                output.tokens.push_back(
                    {TokenKind::Newline,
                     {outcome.span.offset, outcome.span.length}, false});
                if (wants_delimiter) {
                    fail(ParseStatus::Malformed, outcome.span.offset,
                         "missing here-document delimiter");
                    return;
                }
                if (!pending.empty()) {
                    documents();
                    if (diagnostic.status != ParseStatus::Complete) return;
                    // The bodies are text, not tokens: resume after them.
                    cursor = mm::parse::Cursor{source, at,
                                               mm::parse::Dialect::Full};
                }
                continue;
            }
            if (outcome.kind == mm::parse::TokenKind::HereDocument) {
                waiting = output.tokens.size() - 1;
                wants_delimiter = true;
                continue;
            }
            if (outcome.kind == mm::parse::TokenKind::Word &&
                wants_delimiter) {
                handle_heredoc_delimiter(outcome.span.offset,
                                          outcome.span.length);
                if (diagnostic.status != ParseStatus::Complete) return;
                continue;
            }
            if (!outcome.complete) {
                fail(refusal_status(outcome.message), outcome.span.offset,
                     outcome.message);
                return;
            }
        }
        if (diagnostic.status == ParseStatus::Complete &&
            wants_delimiter) {
            fail(ParseStatus::Incomplete, at,
                 "missing here-document delimiter");
        }
        if (diagnostic.status == ParseStatus::Complete &&
            !pending.empty()) {
            fail(ParseStatus::Incomplete, at,
                 "missing here-document body");
        }
        output.tokens.push_back({TokenKind::End, {at, 0}, false});
    }
};

}  // namespace

Diagnostic scan_full(std::string_view text, FullScript& output) {
    output = {};
    output.source.assign(text);
    Scanner scanner{output, output.source};
    scanner.run();
    return scanner.diagnostic;
}

}  // namespace mm::shell::full
