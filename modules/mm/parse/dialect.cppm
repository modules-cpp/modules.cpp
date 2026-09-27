// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.parse:dialect;

export namespace mm::parse {

// Which token dialect the scanner produces.
enum class Dialect {
    Embedded,  // mm.shell level-1: fragments, \r\n and bare \r are line ends
    Full,      // mm.shell.full level-3: no fragments, \r is blank
};

// Token-kind superset. The embedded dialect uses a subset; the full dialect
// uses the whole set. Both existing enums map 1:1 into this.
enum class TokenKind {
    End,
    Newline,
    Word,
    // Embedded-only tokens:
    AndIf,
    OrIf,
    Semicolon,
    DoubleSemicolon,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Bang,
    CaseBar,
    // Full-only tokens:
    Pipe,
    Input,
    Output,
    Append,
    HereDocument,
    DuplicateInput,
    DuplicateOutput,
    IoNumber,
};

// Fragment kinds: the embedded dialect produces these; the full dialect
// does not.
enum class FragmentKind {
    Literal,
    SingleQuoted,
    DoubleQuoted,
    Escaped,
    Parameter,
    Arithmetic,
    CommandSubstitution,
};

// A span into source text: byte offset and length.
struct SourceSpan {
    std::size_t offset = 0;
    std::size_t length = 0;
};

// Function-pointer seam between the two dialects. No vtable, no allocation.
// The embedded dialect's Writer struct is the context; the full dialect's
// Scanner::emit is the context. mm.parse knows nothing about either.
struct Sink {
    void* context = nullptr;
    void (*emit_token)(void* ctx, TokenKind kind, SourceSpan span,
                       bool quoted) = nullptr;
    void (*emit_fragment)(void* ctx, FragmentKind kind, SourceSpan span,
                           bool quoted) = nullptr;
    bool has_unquoted_glob = false;
};

// Shared character-class predicates, defined once.
[[nodiscard]] inline bool blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }
[[nodiscard]] inline bool line_end(char c) { return c == '\n' || c == '\r'; }
[[nodiscard]] inline bool operator_start(char c) {
    return c == '&' || c == '|' || c == ';' || c == '(' ||
           c == ')' || c == '<' || c == '>';
}
[[nodiscard]] inline bool brace_operator(std::string_view text, std::size_t at) {
    if (text.empty() || at >= text.size()) return false;
    if (text[at] != '{' && text[at] != '}') return false;
    if (at + 1 >= text.size()) return true;
    const char next = text[at + 1];
    return next == ' ' || next == '\t' || next == '\n' ||
           next == '\r' || next == ';' ||
           (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z');
}
[[nodiscard]] inline bool is_continuation(std::string_view text, std::size_t at) {
    if (text.empty() || at >= text.size()) return false;
    if (text[at] != '\\') return false;
    if (at + 1 < text.size() && text[at + 1] == '\n') return true;
    if (at + 2 < text.size() && text[at + 1] == '\r' && text[at + 2] == '\n') return true;
    return false;
}

}  // namespace mm::parse
