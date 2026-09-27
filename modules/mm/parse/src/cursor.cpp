// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

module mm.parse;

import :dialect;
import :cursor;

namespace mm::parse {

// Dialect-specific predicates. The embedded dialect treats \r\n and bare \r
// as line endings and space/tab as blank. The full dialect treats \r as blank
// and only \n as a line ending.
[[nodiscard]] bool cursor_line_end(char c, Dialect d) {
    return d == Dialect::Embedded ? (c == '\n' || c == '\r') : (c == '\n');
}

[[nodiscard]] bool cursor_blank(char c, Dialect d) {
    return d == Dialect::Embedded ? (c == ' ' || c == '\t')
                                  : (c == ' ' || c == '\t' || c == '\r');
}

namespace {

[[nodiscard]] bool name_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c == '_');
}

[[nodiscard]] bool name_rest(char c) {
    return name_first(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool special(char c) {
    return c == '#' || c == '?' || c == '$' || c == '*' || c == '@';
}

}  // namespace

Cursor::Cursor(std::string_view text, std::size_t at, Dialect dialect)
    : text_{text}, at_{at}, dialect_{dialect} {}

void Cursor::skip_insignificant() {
    while (at_ < text_.size()) {
        const char c = text_[at_];
        if (cursor_blank(c, dialect_)) {
            ++at_;
        } else if (mm::parse::is_continuation(text_, at_)) {
            skip_continuation();
        } else if (c == '#') {
            while (at_ < text_.size() && !cursor_line_end(text_[at_], dialect_))
                ++at_;
        } else {
            break;
        }
    }
}

bool Cursor::is_continuation() const {
    return mm::parse::is_continuation(text_, at_);
}

void Cursor::skip_continuation() {
    at_ += text_[at_ + 1] == '\r' ? 3 : 2;
}

void Cursor::skip_dollar_parentheses() {
    at_ += 2;
    std::size_t depth = 1;
    char quote = 0;
    while (at_ < text_.size()) {
        const char c = text_[at_];
        if (c == '\\' && at_ + 1 < text_.size()) {
            at_ += 2;
            continue;
        }
        if (quote != 0) {
            if (c == quote) quote = 0;
            ++at_;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            ++at_;
            return;
        }
        ++at_;
    }
    error_ = true;
    error_at_ = at_;
}

void Cursor::skip_dollar_braces() {
    at_ += 2;
    std::size_t depth = 1;
    while (at_ < text_.size()) {
        if (text_[at_] == '\\' && at_ + 1 < text_.size()) {
            at_ += 2;
            continue;
        }
        if (text_[at_] == '{') ++depth;
        if (text_[at_] == '}' && --depth == 0) {
            ++at_;
            return;
        }
        ++at_;
    }
    error_ = true;
    error_at_ = at_;
}

void Cursor::skip_quote(char delimiter, Sink& sink) {
    const auto start = at_++;
    std::size_t literal = at_;
    bool emitted = false;
    while (at_ < text_.size()) {
        const char c = text_[at_];
        if (c == delimiter) {
            if (at_ > literal || !emitted) {
                SourceSpan span{literal, at_ - literal};
                if (sink.emit_fragment)
                    sink.emit_fragment(
                        sink.context,
                        delimiter == '\'' ? FragmentKind::SingleQuoted
                                          : FragmentKind::DoubleQuoted,
                        span, true);
            }
            ++at_;
            return;
        }
        if (delimiter == '"' && c == '$' && at_ + 1 < text_.size()) {
            if (at_ > literal) {
                SourceSpan span{literal, at_ - literal};
                if (sink.emit_fragment)
                    sink.emit_fragment(sink.context, FragmentKind::DoubleQuoted,
                                       span, true);
                emitted = true;
            }
            if (text_[at_ + 1] == '(') {
                skip_dollar_parentheses();
            } else if (text_[at_ + 1] == '{') {
                skip_dollar_braces();
            } else {
                expansion(true, sink);
            }
            if (error_) return;
            emitted = true;
            literal = at_;
            continue;
        }
        if (delimiter == '"' && c == '\\' && at_ + 1 < text_.size() &&
            (text_[at_ + 1] == '$' || text_[at_ + 1] == '"' ||
             text_[at_ + 1] == '\\' || text_[at_ + 1] == '`')) {
            if (at_ > literal) {
                SourceSpan span{literal, at_ - literal};
                if (sink.emit_fragment)
                    sink.emit_fragment(sink.context, FragmentKind::DoubleQuoted,
                                       span, true);
                emitted = true;
            }
            if (sink.emit_fragment)
                sink.emit_fragment(sink.context, FragmentKind::Escaped,
                                   {at_ + 1, 1}, true);
            at_ += 2;
            emitted = true;
            literal = at_;
            continue;
        }
        if (c == '\0') {
            error_ = true;
            error_at_ = at_;
            return;
        }
        if (c == '`' && delimiter == '"' && dialect_ == Dialect::Full) {
            error_ = true;
            error_at_ = at_;
            error_message = "backtick substitution is not supported";
            return;
        }
        if (c == '\\' && delimiter == '"' && at_ + 1 < text_.size() &&
            dialect_ == Dialect::Full) {
            at_ += 2;
            continue;
        }
        ++at_;
    }
    error_ = true;
    error_at_ = start;
    error_message = "unclosed quote";
}

void Cursor::expansion(bool quoted, Sink& sink) {
    const auto start = at_++;
    if (at_ == text_.size()) {
        if (sink.emit_fragment)
            sink.emit_fragment(sink.context,
                               quoted ? FragmentKind::DoubleQuoted
                                      : FragmentKind::Literal,
                               {start, 1}, quoted);
        return;
    }
    if (text_[at_] == '{') {
        ++at_;
        unsigned int depth = 1;
        while (at_ < text_.size() && depth != 0) {
            if (text_[at_] == '{') ++depth;
            if (text_[at_] == '}') --depth;
            ++at_;
        }
        if (depth != 0) {
            error_ = true;
            error_at_ = start;
            error_message = "unclosed parameter expansion";
            return;
        }
        if (sink.emit_fragment)
            sink.emit_fragment(sink.context, FragmentKind::Parameter,
                               {start, at_ - start}, quoted);
        return;
    }
    if (text_[at_] == '(') {
        const bool arithmetic = at_ + 1 < text_.size() && text_[at_ + 1] == '(';
        at_ += arithmetic ? 2 : 1;
        unsigned int depth = arithmetic ? 2 : 1;
        char quote = 0;
        while (at_ < text_.size() && depth != 0) {
            const char c = text_[at_];
            if (c == '\\' && at_ + 1 < text_.size()) {
                at_ += 2;
                continue;
            }
            if (quote != 0) {
                if (c == quote) quote = 0;
                ++at_;
                continue;
            }
            if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            }
            ++at_;
        }
        if (depth != 0 || quote != 0) {
            error_ = true;
            error_at_ = start;
            error_message = "unclosed command or arithmetic substitution";
            return;
        }
        if (sink.emit_fragment)
            sink.emit_fragment(
                sink.context,
                arithmetic ? FragmentKind::Arithmetic
                           : FragmentKind::CommandSubstitution,
                {start, at_ - start}, quoted);
        return;
    }
    if (name_first(text_[at_])) {
        do { ++at_; } while (at_ < text_.size() && name_rest(text_[at_]));
    } else if ((text_[at_] >= '0' && text_[at_] <= '9') ||
               special(text_[at_])) {
        ++at_;
    } else {
        if (sink.emit_fragment)
            sink.emit_fragment(sink.context,
                               quoted ? FragmentKind::DoubleQuoted
                                      : FragmentKind::Literal,
                               {start, 1}, quoted);
        return;
    }
    if (sink.emit_fragment)
        sink.emit_fragment(sink.context, FragmentKind::Parameter,
                           {start, at_ - start}, quoted);
}

void Cursor::scan_word(Sink& sink) {
    std::size_t literal = at_;
    while (at_ < text_.size()) {
        const char c = text_[at_];
        if (c == '[') {
            auto close = at_ + 1;
            while (close < text_.size() &&
                   !cursor_blank(text_[close], dialect_) &&
                   !cursor_line_end(text_[close], dialect_)) {
                if (text_[close] == '\\' && close + 1 < text_.size()) {
                    close += 2;
                    continue;
                }
                if (text_[close] == ']') break;
                ++close;
            }
            if (close < text_.size() && text_[close] == ']') {
                at_ = close + 1;
                continue;
            }
        }
        if (cursor_blank(c, dialect_) || cursor_line_end(c, dialect_) ||
            operator_start(c) || brace_operator(text_, at_))
            break;
        if (c == '\0') {
            error_ = true;
            error_at_ = at_;
            error_message = "NUL in script";
            return;
        }
        if (c == '`') {
            error_ = true;
            error_at_ = at_;
            error_message =
                dialect_ == Dialect::Full
                    ? "backtick substitution is not supported"
                    : "backtick substitution is not supported";
            return;
        }
        if (c == '\\' || c == '\'' || c == '"' || c == '$') {
            if (at_ > literal) {
                SourceSpan span{literal, at_ - literal};
                if (sink.emit_fragment)
                    sink.emit_fragment(sink.context, FragmentKind::Literal,
                                       span, false);
            }
            if (c == '\\') {
                if (at_ + 1 == text_.size()) {
                    error_ = true;
                    error_at_ = at_;
                    error_message = "trailing escape";
                    return;
                }
                if (is_continuation()) {
                    skip_continuation();
                } else {
                    if (sink.emit_fragment)
                        sink.emit_fragment(sink.context, FragmentKind::Escaped,
                                           {at_ + 1, 1}, false);
                    at_ += 2;
                }
            } else if (c == '$') {
                expansion(false, sink);
            } else {
                skip_quote(c, sink);
            }
            if (error_) return;
            literal = at_;
            continue;
        }
        ++at_;
    }
    if (at_ > literal) {
        SourceSpan span{literal, at_ - literal};
        if (sink.emit_fragment)
            sink.emit_fragment(sink.context, FragmentKind::Literal, span,
                               false);
    }
    // Full dialect: check for excluded constructs.
    if (dialect_ == Dialect::Full) {
        const auto spelling = text_.substr(literal, at_ - literal);
        if (spelling == "[[" || spelling == "]]") {
            error_ = true;
            error_at_ = literal;
            error_message = "excluded shell construct";
        }
    }
}

ScanOutcome Cursor::scan(Sink& sink) {
    skip_insignificant();
    const auto start = at_;
    if (start == text_.size()) {
        return {.complete = true,
                .at_end = true,
                .kind = TokenKind::End,
                .span = {start, 0},
                .next_offset = start};
    }
    const char c = text_[start];
    ScanOutcome result;
    result.span = {start, 0};
    result.next_offset = at_;

    if (c == '\0') {
        error_ = true;
        error_at_ = start;
        result.complete = false;
        return result;
    }

    // Newline: dialect-specific.
    if (cursor_line_end(c, dialect_)) {
        if (dialect_ == Dialect::Embedded) {
            if (c == '\r' &&
                (start + 1 == text_.size() || text_[start + 1] != '\n')) {
                error_ = true;
                error_at_ = start;
                result.complete = false;
                return result;
            }
            at_ += c == '\r' && start + 1 < text_.size() &&
                           text_[start + 1] == '\n'
                        ? 2
                        : 1;
        } else {
            ++at_;
        }
        result.kind = TokenKind::Newline;
        result.span = {start, at_ - start};
        result.next_offset = at_;
        return result;
    }

    // Brace operators.
    if ((c == '{' || c == '}') &&
        (start + 1 == text_.size() ||
         cursor_blank(text_[start + 1], dialect_) ||
         cursor_line_end(text_[start + 1], dialect_) ||
         text_[start + 1] == ';')) {
        ++at_;
        result.kind = c == '{' ? TokenKind::LeftBrace : TokenKind::RightBrace;
        result.span = {start, 1};
        result.next_offset = at_;
        if (sink.emit_token)
            sink.emit_token(sink.context, result.kind, result.span, false);
        return result;
    }

    // Two-character operators (full dialect).
    if (dialect_ == Dialect::Full && start + 1 < text_.size()) {
        const auto pair = text_.substr(start, 2);
        if (pair == "&&") {
            at_ += 2;
            result.kind = TokenKind::AndIf;
        } else if (pair == "||") {
            at_ += 2;
            result.kind = TokenKind::OrIf;
        } else if (pair == ";;") {
            at_ += 2;
            result.kind = TokenKind::DoubleSemicolon;
        } else if (pair == ">>") {
            at_ += 2;
            result.kind = TokenKind::Append;
        } else if (pair == "<<") {
            if (at_ + 2 < text_.size() &&
                (text_[at_ + 2] == '-' || text_[at_ + 2] == '<')) {
                error_ = true;
                error_at_ = start;
                error_message = "here-document variant is not supported";
                result.complete = false;
                return result;
            }
            at_ += 2;
            result.kind = TokenKind::HereDocument;
        } else if (pair == "<&") {
            at_ += 2;
            result.kind = TokenKind::DuplicateInput;
        } else if (pair == ">&") {
            at_ += 2;
            result.kind = TokenKind::DuplicateOutput;
        } else if (pair == "((" || pair == "<(" || pair == ">(") {
            error_ = true;
            error_at_ = start;
            error_message = "excluded shell construct";
            result.complete = false;
            return result;
        } else if (c == ';') {
            at_ += 1;
            result.kind = TokenKind::Semicolon;
        } else if (c == '|') {
            at_ += 1;
            result.kind = TokenKind::Pipe;
        } else if (c == '(' || c == ')') {
            at_ += 1;
            result.kind = c == '(' ? TokenKind::LeftParen : TokenKind::RightParen;
        } else if (c == '<') {
            at_ += 1;
            result.kind = TokenKind::Input;
        } else if (c == '>') {
            at_ += 1;
            result.kind = TokenKind::Output;
        } else if (c == '&') {
            error_ = true;
            error_at_ = start;
            error_message = "asynchronous execution is not supported";
            result.complete = false;
            return result;
        } else {
            // Not an operator: fall through to word scanning.
            goto word_scan;
        }
        result.span = {start, at_ - start};
        result.next_offset = at_;
        if (sink.emit_token)
            sink.emit_token(sink.context, result.kind, result.span, false);
        return result;
    }

    // Embedded dialect: two-character operators.
    if (dialect_ == Dialect::Embedded) {
        if (c == '&' && start + 1 < text_.size() && text_[start + 1] == '&') {
            at_ += 2;
            result.kind = TokenKind::AndIf;
        } else if (c == '|' && start + 1 < text_.size() &&
                   text_[start + 1] == '|') {
            at_ += 2;
            result.kind = TokenKind::OrIf;
        } else if (c == ';') {
            at_ += start + 1 < text_.size() && text_[start + 1] == ';' ? 2 : 1;
            result.kind = at_ - start == 2 ? TokenKind::DoubleSemicolon
                                            : TokenKind::Semicolon;
        } else if (c == '(' || c == ')') {
            ++at_;
            result.kind = c == '(' ? TokenKind::LeftParen
                                    : TokenKind::RightParen;
        } else if (c == '!' &&
                   (start + 1 == text_.size() ||
                    cursor_blank(text_[start + 1], dialect_) ||
                    cursor_line_end(text_[start + 1], dialect_) ||
                    operator_start(text_[start + 1]) ||
                    brace_operator(text_, start + 1))) {
            ++at_;
            result.kind = TokenKind::Bang;
        } else if (c == '|') {
            ++at_;
            result.kind = TokenKind::CaseBar;
        } else if (c == '&' || c == '<' || c == '>' || c == '`') {
            error_ = true;
            error_at_ = start;
            result.complete = false;
            return result;
        } else {
            goto word_scan;
        }
        result.span = {start, at_ - start};
        result.next_offset = at_;
        if (sink.emit_token)
            sink.emit_token(sink.context, result.kind, result.span, false);
        return result;
    }

word_scan:
    // Word scanning (both dialects).
    {
        bool is_io_number = false;
        if (dialect_ == Dialect::Full) {
            auto pos = start;
            while (pos < text_.size() && (text_[pos] >= '0' &&
                                          text_[pos] <= '9'))
                ++pos;
            if (pos > start && pos < text_.size() &&
                (text_[pos] == '<' || text_[pos] == '>') &&
                (pos == start ||
                 cursor_blank(text_[pos - 1], dialect_))) {
                is_io_number = true;
            }
        }
        scan_word(sink);
        if (error_) {
            result.complete = false;
            result.message = error_message;
            return result;
        }
        result.kind = is_io_number ? TokenKind::IoNumber : TokenKind::Word;
    }
    result.span = {start, at_ - start};
    result.next_offset = at_;
    if (sink.emit_token)
        sink.emit_token(sink.context, result.kind, result.span, false);
    return result;
}

}  // namespace mm::parse
