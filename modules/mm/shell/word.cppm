// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:word;

import :source;
import :status;
import mm.parse;

export namespace mm::shell {

enum class ScanStatus {
    Complete,
    Incomplete,
    Malformed,
    Overflow,
};

// The embedded dialect's token kinds. These are a subset of
// mm::parse::TokenKind; the to_shell_token adapter in src/word.cpp
// translates between the two.
using TokenKind = mm::parse::TokenKind;

// The embedded dialect's fragment kinds. Identical to
// mm::parse::FragmentKind; the to_shell_fragment adapter in src/word.cpp
// is a pass-through.
using FragmentKind = mm::parse::FragmentKind;

struct WordFragment {
    FragmentKind kind = FragmentKind::Literal;
    mm::parse::SourceSpan source;
    bool quoted = false;
};

struct ScanToken {
    TokenKind kind = TokenKind::End;
    mm::parse::SourceSpan source;
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
