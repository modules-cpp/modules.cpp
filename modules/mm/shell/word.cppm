// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:word;

import :source;
import :status;

export namespace mm::shell {

enum class ScanStatus {
    Complete,
    Incomplete,
    Malformed,
    Overflow,
};

enum class TokenKind {
    End,
    Newline,
    Word,
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
};

enum class FragmentKind {
    Literal,
    SingleQuoted,
    DoubleQuoted,
    Escaped,
    Parameter,
    Arithmetic,
    CommandSubstitution,
};

struct WordFragment {
    FragmentKind kind = FragmentKind::Literal;
    SourceSpan source;
    bool quoted = false;
};

struct ScanToken {
    TokenKind kind = TokenKind::End;
    SourceSpan source;
    std::size_t next_offset = 0;
    std::size_t fragments_required = 0;
    bool has_unquoted_glob = false;
};

struct ScanOutcome {
    ScanStatus status = ScanStatus::Complete;
    ScanToken token;
    SourceLocation issue;
    OverflowInfo overflow;
};

// Empty fragments means count only. A nonempty span is filled only when the
// whole token is valid and fits. The returned source spans view SourceView.
[[nodiscard]] ScanOutcome scan_embedded(
    SourceView source,
    std::size_t offset,
    std::span<WordFragment> fragments = {});

// Re-scan one fragment without requiring a variable-size temporary array.
// Returns Malformed when index is outside the token's fragment sequence.
[[nodiscard]] ScanOutcome scan_embedded_fragment(
    SourceView source, std::size_t offset, std::size_t index,
    WordFragment& fragment);

}  // namespace mm::shell
