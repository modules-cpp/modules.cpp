// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.parse:cursor;

import :dialect;

export namespace mm::parse {

// Scan outcome for a single token.
struct ScanOutcome {
    bool complete = true;
    bool at_end = false;
    TokenKind kind = TokenKind::End;
    SourceSpan span;
    std::size_t next_offset = 0;
    std::size_t fragment_count = 0;
    bool has_unquoted_glob = false;
};

// A stateless cursor over source text. No allocation, no output ownership:
// the caller supplies a Sink that receives the token and any fragments.
// The dialect controls line-ending behavior and whether fragments are
// produced.
class Cursor {
public:
    Cursor(std::string_view text, std::size_t at, Dialect dialect);

    // Advance past blanks, comments, and line continuations.
    void skip_insignificant();

    // Scan one token. The Sink receives the token kind, span, and any
    // fragments (embedded dialect only). Returns the outcome.
    [[nodiscard]] ScanOutcome scan(Sink& sink);

    std::size_t offset() const { return at_; }
    bool at_end() const { return at_ >= text_.size(); }

private:
    void scan_word(Sink& sink);
    void skip_quote(char delimiter, Sink& sink);
    void skip_dollar_parentheses();
    void skip_dollar_braces();
    void expansion(bool quoted, Sink& sink);
    bool is_continuation() const;
    void skip_continuation();

    std::string_view text_;
    std::size_t at_ = 0;
    Dialect dialect_;
    bool error_ = false;
    std::size_t error_at_ = 0;
};

}  // namespace mm::parse
