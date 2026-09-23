// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:syntax;

export namespace mm::shell::full {

enum class TokenKind {
    Word, IoNumber, Newline, Semicolon, DoubleSemicolon, AndIf, OrIf, Pipe,
    OpenParen, CloseParen, OpenBrace, CloseBrace, Input, Output,
    Append, HereDocument, DuplicateInput, DuplicateOutput, Bang, End,
};

struct SourceRange {
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct Token {
    TokenKind kind = TokenKind::End;
    SourceRange source;
    bool quoted = false;
};

struct HereDocument {
    std::size_t operator_token = 0;
    SourceRange body;
    std::string delimiter;
    bool expand = true;
};

enum class NodeKind {
    Program, List, AndOr, Pipeline, Simple, If, While, For,
    Case, CaseItem, Function, BraceGroup, Subshell, Negation,
    Redirection,
};

enum class Join { Sequence, And, Or, Pipe, Alternative };

struct Child {
    std::size_t node = 0;
    Join join = Join::Sequence;
};

struct Node {
    NodeKind kind = NodeKind::Program;
    std::size_t first_token = 0;
    std::size_t last_token = 0;
    std::vector<Child> children;
};

// All ranges index this object's source. Moving it cannot dangle syntax.
struct FullScript {
    std::string source;
    std::vector<Token> tokens;
    std::vector<HereDocument> documents;
    std::vector<Node> nodes;
    std::size_t root = 0;

    [[nodiscard]] std::string_view text(SourceRange range) const {
        return std::string_view{source}.substr(range.offset,
                                               range.length);
    }
};

enum class ParseStatus { Complete, Incomplete, Malformed, Unsupported };

struct Diagnostic {
    ParseStatus status = ParseStatus::Complete;
    std::size_t offset = 0;
    std::string message;
};

}  // namespace mm::shell::full
