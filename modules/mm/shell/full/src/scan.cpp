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

namespace mm::shell::full {
namespace {

struct PendingDocument {
    std::size_t operator_token = 0;
    std::string delimiter;
    bool expand = true;
};

[[nodiscard]] bool blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

[[nodiscard]] bool operator_start(char c) {
    return c == ';' || c == '|' || c == '&' || c == '(' ||
           c == ')' || c == '<' || c == '>';
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

    void emit(TokenKind kind, std::size_t start,
              bool quoted = false) {
        output.tokens.push_back({kind, {start, at - start}, quoted});
    }

    // Skip a balanced $(...) or $((...)) without mistaking its operators
    // for operators in the containing script. Quotes belong to this scope.
    void dollar_parentheses() {
        const auto start = at;
        at += 2;
        std::size_t depth = 1;
        char quote = 0;
        while (at < source.size()) {
            const char c = source[at];
            if (c == '\\') {
                at += at + 1 < source.size() ? 2 : 1;
                continue;
            }
            if (quote == '\'') {
                if (c == quote) quote = 0;
                ++at;
                continue;
            }
            if (quote == '"') {
                if (c == '"') quote = 0;
                ++at;
                continue;
            }
            if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')' && --depth == 0) {
                ++at;
                return;
            }
            ++at;
        }
        fail(ParseStatus::Incomplete, start,
             "unclosed command or arithmetic substitution");
    }

    void dollar_braces() {
        const auto start = at;
        at += 2;
        std::size_t depth = 1;
        while (at < source.size()) {
            if (source[at] == '\\' && at + 1 < source.size()) {
                at += 2;
                continue;
            }
            if (source[at] == '{') ++depth;
            if (source[at] == '}' && --depth == 0) {
                ++at;
                return;
            }
            ++at;
        }
        fail(ParseStatus::Incomplete, start,
             "unclosed parameter expansion");
    }

    void quote(char delimiter, bool& quoted) {
        const auto start = at++;
        quoted = true;
        while (at < source.size()) {
            if (source[at] == delimiter) {
                ++at;
                return;
            }
            if (delimiter == '"' && source[at] == '$' &&
                at + 1 < source.size()) {
                if (source[at + 1] == '(') {
                    dollar_parentheses();
                    if (diagnostic.status != ParseStatus::Complete) return;
                    continue;
                }
                if (source[at + 1] == '{') {
                    dollar_braces();
                    if (diagnostic.status != ParseStatus::Complete) return;
                    continue;
                }
            }
            if (delimiter == '"' && source[at] == '`') {
                fail(ParseStatus::Unsupported, at,
                     "backtick substitution is not supported");
                return;
            }
            if (delimiter == '"' && source[at] == '\\' &&
                at + 1 < source.size()) {
                at += 2;
            } else {
                ++at;
            }
        }
        fail(ParseStatus::Incomplete, start, "unclosed quote");
    }

    void word() {
        const auto start = at;
        bool quoted = false;
        while (at < source.size()) {
            const char c = source[at];
            if (blank(c) || c == '\n' || operator_start(c)) break;
            if (c == '\0') {
                fail(ParseStatus::Malformed, at, "NUL in script");
                return;
            }
            if (c == '`') {
                fail(ParseStatus::Unsupported, at,
                     "backtick substitution is not supported");
                return;
            }
            if (c == '\\') {
                quoted = true;
                if (at + 1 == source.size()) {
                    fail(ParseStatus::Incomplete, at, "trailing escape");
                    return;
                }
                at += 2;
                continue;
            }
            if (c == '\'' || c == '"') {
                quote(c, quoted);
                if (diagnostic.status != ParseStatus::Complete) return;
                continue;
            }
            if (c == '$' && at + 1 < source.size()) {
                if (source[at + 1] == '(') {
                    dollar_parentheses();
                    if (diagnostic.status != ParseStatus::Complete) return;
                    continue;
                }
                if (source[at + 1] == '{') {
                    dollar_braces();
                    if (diagnostic.status != ParseStatus::Complete) return;
                    continue;
                }
            }
            ++at;
        }
        const auto spelling = source.substr(start, at - start);
        if (spelling == "[[" || spelling == "]]") {
            fail(ParseStatus::Unsupported, start,
                 "excluded shell construct");
            return;
        }
        bool io_number = !quoted && !spelling.empty() &&
                         at < source.size() &&
                         (source[at] == '<' || source[at] == '>');
        for (const char digit : spelling) {
            if (digit < '0' || digit > '9') io_number = false;
        }
        emit(io_number ? TokenKind::IoNumber : TokenKind::Word,
             start, quoted);
        if (wants_delimiter) {
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
                fail(ParseStatus::Malformed, start,
                     "empty here-document delimiter");
                return;
            }
            pending.push_back({waiting, std::move(delimiter),
                               !quoted_delimiter});
            wants_delimiter = false;
        }
    }

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

    void run() {
        while (at < source.size() &&
               diagnostic.status == ParseStatus::Complete) {
            if (blank(source[at])) { ++at; continue; }
            if (source[at] == '#') {
                while (at < source.size() && source[at] != '\n') ++at;
                continue;
            }
            const auto start = at;
            if (source[at] == '\n') {
                ++at;
                emit(TokenKind::Newline, start);
                if (wants_delimiter) {
                    fail(ParseStatus::Malformed, start,
                         "missing here-document delimiter");
                } else if (!pending.empty()) {
                    documents();
                }
                continue;
            }
            if ((source[at] == '{' || source[at] == '}') &&
                (at + 1 == source.size() || blank(source[at + 1]) ||
                 source[at + 1] == '\n' || source[at + 1] == ';')) {
                const auto kind = source[at++] == '{'
                    ? TokenKind::OpenBrace : TokenKind::CloseBrace;
                emit(kind, start);
                continue;
            }
            if (!operator_start(source[at])) {
                word();
                continue;
            }
            if (at + 1 < source.size()) {
                const auto pair = source.substr(at, 2);
                if (pair == "&&") { at += 2; emit(TokenKind::AndIf, start);
                    continue; }
                if (pair == "||") { at += 2; emit(TokenKind::OrIf, start);
                    continue; }
                if (pair == ";;") { at += 2;
                    emit(TokenKind::DoubleSemicolon, start); continue; }
                if (pair == ">>") { at += 2;
                    emit(TokenKind::Append, start); continue; }
                if (pair == "<<") {
                    if (at + 2 < source.size() &&
                        (source[at + 2] == '-' ||
                         source[at + 2] == '<')) {
                        fail(ParseStatus::Unsupported, start,
                             "here-document variant is not supported");
                        continue;
                    }
                    at += 2;
                    emit(TokenKind::HereDocument, start);
                    waiting = output.tokens.size() - 1;
                    wants_delimiter = true;
                    continue;
                }
                if (pair == "<&") { at += 2;
                    emit(TokenKind::DuplicateInput, start); continue; }
                if (pair == ">&") { at += 2;
                    emit(TokenKind::DuplicateOutput, start); continue; }
                if (pair == "((" || pair == "<(" || pair == ">(") {
                    fail(ParseStatus::Unsupported, start,
                         "excluded shell construct");
                    continue;
                }
            }
            const char c = source[at++];
            if (c == '&') {
                fail(ParseStatus::Unsupported, start,
                     "asynchronous execution is not supported");
                continue;
            }
            TokenKind kind = TokenKind::Word;
            switch (c) {
                case ';': kind = TokenKind::Semicolon; break;
                case '|': kind = TokenKind::Pipe; break;
                case '(': kind = TokenKind::OpenParen; break;
                case ')': kind = TokenKind::CloseParen; break;
                case '<': kind = TokenKind::Input; break;
                case '>': kind = TokenKind::Output; break;
                default: break;
            }
            emit(kind, start);
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
        emit(TokenKind::End, at);
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
