// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:syntax;

import :source;
import :word;

export namespace mm::shell {

enum class SyntaxKind {
    Program,
    List,
    AndOr,
    Negation,
    Simple,
    Word,
    Assignment,
    If,
    Elif,
    Else,
    For,
    While,
    Case,
    CaseItem,
    Pattern,
    Function,
    BraceGroup,
};

enum class SyntaxJoin { Sequence, And, Or, Alternative };

struct ScriptToken {
    TokenKind kind = TokenKind::End;
    SourceSpan source;
    std::size_t fragment_first = 0;
    std::size_t fragment_count = 0;
};

struct SyntaxNode {
    SyntaxKind kind = SyntaxKind::Program;
    SourceSpan source;
    std::size_t token_index = 0;
    std::size_t first_link = 0;
    std::size_t last_link = 0;
    std::size_t link_count = 0;
    bool has_in_clause = false;
};

struct SyntaxLink {
    std::size_t child = 0;
    std::size_t next = 0;
    SyntaxJoin join = SyntaxJoin::Sequence;
};

// One caller-owned slot accounts for each open grammar production.
struct ParserFrame {
    SyntaxKind kind = SyntaxKind::Program;
};

struct ScriptStorage {
    std::span<ScriptToken> tokens;
    std::span<WordFragment> fragments;
    std::span<SyntaxNode> nodes;
    std::span<SyntaxLink> links;
    std::span<ParserFrame> context;
};

struct EmbeddedScript {
    SourceView source;
    std::span<const ScriptToken> tokens;
    std::span<const WordFragment> fragments;
    std::span<const SyntaxNode> nodes;
    std::span<const SyntaxLink> links;
    std::size_t root = 0;
};

struct ParseCounts {
    std::size_t tokens = 0;
    std::size_t fragments = 0;
    std::size_t nodes = 0;
    std::size_t links = 0;
    std::size_t context = 0;
};

}  // namespace mm::shell
