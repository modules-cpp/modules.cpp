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

namespace mm::shell {
namespace {

[[nodiscard]] bool name_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_rest(char c) {
    return name_first(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool blank(char c) { return c == ' ' || c == '\t'; }

[[nodiscard]] bool line_end(char c) { return c == '\n' || c == '\r'; }

[[nodiscard]] bool special(char c) {
    return c == '#' || c == '?' || c == '$' || c == '*' || c == '@';
}

[[nodiscard]] bool operator_start(char c) {
    return c == '&' || c == '|' || c == ';' || c == '(' ||
           c == ')' || c == '<' || c == '>';
}

[[nodiscard]] bool brace_operator(std::string_view text, std::size_t at) {
    if (text[at] != '{' && text[at] != '}') return false;
    const auto next = at + 1;
    return next == text.size() || blank(text[next]) ||
           line_end(text[next]) || text[next] == ';';
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

struct Cursor {
    std::string_view text;
    std::size_t at = 0;
    std::size_t error = 0;
    ScanStatus status = ScanStatus::Complete;
    Writer& writer;

    [[nodiscard]] bool continuation() const {
        return at + 1 < text.size() && text[at] == '\\' &&
               (text[at + 1] == '\n' ||
                (text[at + 1] == '\r' && at + 2 < text.size() &&
                 text[at + 2] == '\n'));
    }

    void skip_continuation() {
        at += text[at + 1] == '\r' ? 3 : 2;
    }

    void fail(ScanStatus kind, std::size_t where) {
        status = kind;
        error = where;
    }

    void expansion(bool quoted) {
        const auto start = at++;
        if (at == text.size()) {
            writer.add(quoted ? FragmentKind::DoubleQuoted
                              : FragmentKind::Literal, start, 1, quoted);
            return;
        }
        if (text[at] == '{') {
            ++at;
            unsigned int depth = 1;
            while (at < text.size() && depth != 0) {
                if (text[at] == '{') ++depth;
                if (text[at] == '}') --depth;
                ++at;
            }
            if (depth != 0) {
                fail(ScanStatus::Incomplete, start);
                return;
            }
            writer.add(FragmentKind::Parameter, start, at - start, quoted);
            return;
        }
        if (text[at] == '(') {
            const bool arithmetic = at + 1 < text.size() &&
                                    text[at + 1] == '(';
            at += arithmetic ? 2 : 1;
            unsigned int depth = arithmetic ? 2 : 1;
            char quote = 0;
            while (at < text.size() && depth != 0) {
                const char c = text[at];
                if (c == '\\' && at + 1 < text.size()) {
                    at += 2;
                    continue;
                }
                if (quote != 0) {
                    if (c == quote) quote = 0;
                    ++at;
                    continue;
                }
                if (c == '\'' || c == '"') {
                    quote = c;
                } else if (c == '(') {
                    ++depth;
                } else if (c == ')') {
                    --depth;
                }
                ++at;
            }
            if (depth != 0 || quote != 0) {
                fail(ScanStatus::Incomplete, start);
                return;
            }
            writer.add(arithmetic ? FragmentKind::Arithmetic
                                  : FragmentKind::CommandSubstitution,
                       start, at - start, quoted);
            return;
        }
        if (name_first(text[at])) {
            do { ++at; } while (at < text.size() && name_rest(text[at]));
        } else if ((text[at] >= '0' && text[at] <= '9') ||
                   special(text[at])) {
            ++at;
        } else {
            writer.add(quoted ? FragmentKind::DoubleQuoted
                              : FragmentKind::Literal, start, 1, quoted);
            return;
        }
        writer.add(FragmentKind::Parameter, start, at - start, quoted);
    }

    void quote(char delimiter) {
        const auto opening = at++;
        std::size_t literal = at;
        bool emitted = false;
        while (at < text.size()) {
            if (text[at] == delimiter) {
                if (at > literal || !emitted) {
                    writer.add(delimiter == '\'' ? FragmentKind::SingleQuoted
                                                 : FragmentKind::DoubleQuoted,
                               literal, at - literal, true);
                }
                ++at;
                return;
            }
            if (delimiter == '"' && text[at] == '$') {
                if (at > literal) {
                    writer.add(FragmentKind::DoubleQuoted,
                               literal, at - literal, true);
                    emitted = true;
                }
                expansion(true);
                if (status != ScanStatus::Complete) return;
                emitted = true;
                literal = at;
                continue;
            }
            if (delimiter == '"' && text[at] == '\\') {
                if (continuation()) {
                    if (at > literal) {
                        writer.add(FragmentKind::DoubleQuoted,
                                   literal, at - literal, true);
                        emitted = true;
                    }
                    skip_continuation();
                    literal = at;
                    continue;
                }
                if (at + 1 == text.size()) {
                    fail(ScanStatus::Incomplete, opening);
                    return;
                }
                if (text[at + 1] == '$' || text[at + 1] == '"' ||
                    text[at + 1] == '\\' || text[at + 1] == '`') {
                    if (at > literal) {
                        writer.add(FragmentKind::DoubleQuoted,
                                   literal, at - literal, true);
                        emitted = true;
                    }
                    writer.add(FragmentKind::Escaped, at + 1, 1, true);
                    emitted = true;
                    at += 2;
                    literal = at;
                    continue;
                }
            }
            if (text[at] == '\0') {
                fail(ScanStatus::Malformed, at);
                return;
            }
            ++at;
        }
        fail(ScanStatus::Incomplete, opening);
    }

    void word() {
        std::size_t literal = at;
        while (at < text.size()) {
            const char c = text[at];
            if (c == '[') {
                auto close = at + 1;
                while (close < text.size() && !blank(text[close]) &&
                       !line_end(text[close])) {
                    if (text[close] == '\\' && close + 1 < text.size()) {
                        close += 2;
                        continue;
                    }
                    if (text[close] == ']') break;
                    ++close;
                }
                if (close < text.size() && text[close] == ']') {
                    at = close + 1;
                    continue;
                }
            }
            if (blank(c) || line_end(c) || operator_start(c) ||
                brace_operator(text, at)) break;
            if (c == '\0' || c == '`') {
                fail(ScanStatus::Malformed, at);
                return;
            }
            if (c == '\\' || c == '\'' || c == '"' || c == '$') {
                if (at > literal) {
                    writer.add(FragmentKind::Literal,
                               literal, at - literal, false);
                }
                if (c == '\\') {
                    if (at + 1 == text.size()) {
                        fail(ScanStatus::Incomplete, at);
                        return;
                    }
                    if (continuation()) {
                        skip_continuation();
                    } else {
                        writer.add(FragmentKind::Escaped, at + 1, 1, false);
                        at += 2;
                    }
                } else if (c == '$') {
                    expansion(false);
                } else {
                    quote(c);
                }
                if (status != ScanStatus::Complete) return;
                literal = at;
                continue;
            }
            ++at;
        }
        if (at > literal) {
            writer.add(FragmentKind::Literal, literal, at - literal, false);
        }
    }
};

[[nodiscard]] ScanOutcome scan_once(SourceView source,
                                     std::size_t offset,
                                     Writer& writer) {
    const auto text = source.text();
    Cursor cursor{text, offset, 0, ScanStatus::Complete, writer};
    ScanOutcome result;
    while (cursor.at < text.size()) {
        if (blank(text[cursor.at])) {
            ++cursor.at;
        } else if (cursor.continuation()) {
            cursor.skip_continuation();
        } else if (text[cursor.at] == '#') {
            while (cursor.at < text.size() &&
                   !line_end(text[cursor.at])) {
                if (text[cursor.at] == '\0') {
                    cursor.fail(ScanStatus::Malformed, cursor.at);
                    result.status = cursor.status;
                    result.issue = location(text, cursor.error);
                    return result;
                }
                ++cursor.at;
            }
        } else {
            break;
        }
    }
    const auto start = cursor.at;
    if (start == text.size()) {
        result.token = {TokenKind::End, {start, 0}, start, 0};
        return result;
    }
    const char c = text[start];
    if (c == '\0') {
        cursor.fail(ScanStatus::Malformed, start);
    } else if (c == '\r' &&
               (start + 1 == text.size() || text[start + 1] != '\n')) {
        cursor.fail(ScanStatus::Malformed, start);
    } else if (line_end(c)) {
        cursor.at += c == '\r' && start + 1 < text.size() &&
                             text[start + 1] == '\n' ? 2 : 1;
        result.token.kind = TokenKind::Newline;
    } else if (c == '&' && start + 1 < text.size() &&
               text[start + 1] == '&') {
        cursor.at += 2;
        result.token.kind = TokenKind::AndIf;
    } else if (c == '|' && start + 1 < text.size() &&
               text[start + 1] == '|') {
        cursor.at += 2;
        result.token.kind = TokenKind::OrIf;
    } else if (c == ';') {
        cursor.at += start + 1 < text.size() && text[start + 1] == ';' ? 2 : 1;
        result.token.kind = cursor.at - start == 2
                                ? TokenKind::DoubleSemicolon
                                : TokenKind::Semicolon;
    } else if (c == '(' || c == ')') {
        ++cursor.at;
        result.token.kind = c == '(' ? TokenKind::LeftParen
                                   : TokenKind::RightParen;
    } else if (brace_operator(text, start)) {
        ++cursor.at;
        result.token.kind = c == '{' ? TokenKind::LeftBrace
                                   : TokenKind::RightBrace;
    } else if (c == '!' &&
               (start + 1 == text.size() || blank(text[start + 1]) ||
                line_end(text[start + 1]) ||
                operator_start(text[start + 1]) ||
                brace_operator(text, start + 1))) {
        ++cursor.at;
        result.token.kind = TokenKind::Bang;
    } else if (c == '|') {
        ++cursor.at;
        result.token.kind = TokenKind::CaseBar;
    } else if (c == '&' || c == '<' || c == '>' || c == '`') {
        cursor.fail(ScanStatus::Malformed, start);
    } else {
        cursor.word();
        result.token.kind = TokenKind::Word;
    }
    result.status = cursor.status;
    result.issue = location(text, cursor.error);
    result.token.source = {start, cursor.at - start};
    result.token.next_offset = cursor.at;
    result.token.fragments_required = writer.count;
    result.token.has_unquoted_glob = writer.has_unquoted_glob;
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
