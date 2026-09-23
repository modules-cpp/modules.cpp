// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :parse;
import :source;
import :status;
import :syntax;
import :word;

namespace mm::shell {
namespace {

[[nodiscard]] SourceLocation location(SourceView source, std::size_t at) {
    SourceLocation result{.offset = at};
    const auto text = source.text();
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

[[nodiscard]] bool name_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_rest(char c) {
    return name_first(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool valid_name(std::string_view name) {
    if (name.empty() || !name_first(name.front())) return false;
    for (const char c : name.substr(1)) {
        if (!name_rest(c)) return false;
    }
    return true;
}

[[nodiscard]] bool reserved(std::string_view text) {
    return text == "if" || text == "then" || text == "elif" ||
           text == "else" || text == "fi" || text == "for" ||
           text == "in" || text == "do" || text == "done" ||
           text == "while" || text == "case" || text == "esac";
}

struct Sink {
    ScriptStorage* storage = nullptr;
    ParseCounts counts;

    [[nodiscard]] std::size_t node(SyntaxKind kind, SourceSpan source,
                                   std::size_t token = 0) {
        const auto id = counts.nodes++;
        if (storage != nullptr) {
            storage->nodes[id] = {.kind = kind,
                                  .source = source,
                                  .token_index = token};
        }
        return id;
    }

    void end(std::size_t node_id, std::size_t end_offset) {
        if (storage != nullptr) {
            auto& node_ref = storage->nodes[node_id];
            node_ref.source.length = end_offset - node_ref.source.offset;
        }
    }

    void mark_in_clause(std::size_t node_id) {
        if (storage != nullptr) storage->nodes[node_id].has_in_clause = true;
    }

    void link(std::size_t parent, std::size_t child,
              SyntaxJoin join = SyntaxJoin::Sequence) {
        const auto id = counts.links++;
        if (storage == nullptr) return;
        storage->links[id] = {.child = child, .next = 0, .join = join};
        auto& node_ref = storage->nodes[parent];
        if (node_ref.link_count == 0) {
            node_ref.first_link = id;
        } else {
            storage->links[node_ref.last_link].next = id;
        }
        node_ref.last_link = id;
        ++node_ref.link_count;
    }
};

struct Stop {
    std::string_view first;
    std::string_view second;
    std::string_view third;
    bool brace = false;
    bool item = false;
};

class Parser {
public:
    Parser(SourceView source, ScriptStorage* storage)
        : source_(source), sink_{storage} {
        advance();
    }

    [[nodiscard]] MeasureOutcome run() {
        if (status_ != ParseStatus::Complete) return outcome();
        const auto root = sink_.node(SyntaxKind::Program, {0, 0});
        const auto list = parse_list({});
        if (status_ == ParseStatus::Complete) {
            sink_.link(root, list);
            if (current_.kind != TokenKind::End) fail();
        }
        if (status_ == ParseStatus::Complete) {
            sink_.end(root, source_.size());
        }
        return outcome();
    }

private:
    struct Frame {
        Parser& parser;

        Frame(Parser& p, SyntaxKind kind) : parser(p) {
            ++parser.depth_;
            if (parser.depth_ > parser.sink_.counts.context) {
                parser.sink_.counts.context = parser.depth_;
            }
            if (parser.sink_.storage != nullptr) {
                parser.sink_.storage->context[parser.depth_ - 1] = {kind};
            }
        }

        ~Frame() { --parser.depth_; }
    };

    [[nodiscard]] MeasureOutcome outcome() const {
        return {status_, sink_.counts, issue_};
    }

    void advance() {
        if (status_ != ParseStatus::Complete) return;
        const auto measured = scan_embedded(source_, offset_);
        if (measured.status != ScanStatus::Complete) {
            status_ = measured.status == ScanStatus::Incomplete
                          ? ParseStatus::Incomplete
                          : ParseStatus::Malformed;
            issue_ = measured.issue;
            return;
        }
        current_ = measured.token;
        offset_ = current_.next_offset;
        if (current_.kind == TokenKind::End) return;
        const auto token_id = sink_.counts.tokens++;
        if (sink_.storage != nullptr) {
            const auto first = sink_.counts.fragments;
            std::span<WordFragment> fragments;
            if (current_.fragments_required != 0) {
                fragments = sink_.storage->fragments.subspan(
                    first, current_.fragments_required);
                const auto built = scan_embedded(
                    source_, current_.source.offset, fragments);
                if (built.status != ScanStatus::Complete) {
                    status_ = ParseStatus::Malformed;
                    issue_ = built.issue;
                    return;
                }
            }
            sink_.storage->tokens[token_id] = {
                current_.kind, current_.source, first,
                current_.fragments_required};
        }
        sink_.counts.fragments += current_.fragments_required;
    }

    void consume() {
        if (status_ != ParseStatus::Complete) return;
        last_end_ = current_.source.offset + current_.source.length;
        advance();
    }

    void fail() {
        if (status_ != ParseStatus::Complete) return;
        status_ = current_.kind == TokenKind::End
                      ? ParseStatus::Incomplete
                      : ParseStatus::Malformed;
        issue_ = location(source_, current_.source.offset);
    }

    [[nodiscard]] bool word_is(std::string_view text) const {
        return current_.kind == TokenKind::Word &&
               source_.slice(current_.source) == text;
    }

    [[nodiscard]] bool at_stop(const Stop& stop) const {
        return (!stop.first.empty() && word_is(stop.first)) ||
               (!stop.second.empty() && word_is(stop.second)) ||
               (!stop.third.empty() && word_is(stop.third)) ||
               (stop.brace && current_.kind == TokenKind::RightBrace) ||
               (stop.item && current_.kind == TokenKind::DoubleSemicolon);
    }

    [[nodiscard]] bool take(TokenKind kind) {
        if (status_ != ParseStatus::Complete) return false;
        if (current_.kind != kind) {
            fail();
            return false;
        }
        consume();
        return status_ == ParseStatus::Complete;
    }

    [[nodiscard]] bool take_word(std::string_view word) {
        if (status_ != ParseStatus::Complete) return false;
        if (!word_is(word)) {
            fail();
            return false;
        }
        consume();
        return status_ == ParseStatus::Complete;
    }

    void newlines() {
        while (status_ == ParseStatus::Complete &&
               current_.kind == TokenKind::Newline) consume();
    }

    [[nodiscard]] std::size_t parse_list(const Stop& stop,
                                         bool require_command = false) {
        Frame frame{*this, SyntaxKind::List};
        const auto start = current_.source.offset;
        const auto node_id = sink_.node(
            SyntaxKind::List, {start, 0});
        bool saw_command = false;
        newlines();
        while (status_ == ParseStatus::Complete &&
               current_.kind != TokenKind::End && !at_stop(stop)) {
            const auto child = parse_and_or();
            if (status_ != ParseStatus::Complete) break;
            sink_.link(node_id, child);
            saw_command = true;
            if (current_.kind == TokenKind::Semicolon ||
                current_.kind == TokenKind::Newline) {
                consume();
                newlines();
                continue;
            }
            if (current_.kind != TokenKind::End && !at_stop(stop)) {
                fail();
            }
            break;
        }
        if (require_command && !saw_command &&
            status_ == ParseStatus::Complete) fail();
        sink_.end(node_id, last_end_ < start ? start : last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_and_or() {
        Frame frame{*this, SyntaxKind::AndOr};
        const auto node_id = sink_.node(
            SyntaxKind::AndOr, {current_.source.offset, 0});
        const auto first = parse_command();
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, first);
        while (current_.kind == TokenKind::AndIf ||
               current_.kind == TokenKind::OrIf) {
            const auto join = current_.kind == TokenKind::AndIf
                                  ? SyntaxJoin::And : SyntaxJoin::Or;
            consume();
            newlines();
            const auto next = parse_command();
            if (status_ != ParseStatus::Complete) break;
            sink_.link(node_id, next, join);
        }
        sink_.end(node_id, last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_command() {
        Frame frame{*this, SyntaxKind::Simple};
        const auto start = current_.source.offset;
        if (current_.kind == TokenKind::Bang) {
            const auto node_id = sink_.node(SyntaxKind::Negation, {start, 0});
            consume();
            const auto child = parse_command();
            if (status_ == ParseStatus::Complete) {
                sink_.link(node_id, child);
                sink_.end(node_id, last_end_);
            }
            return node_id;
        }
        if (word_is("if")) return parse_if();
        if (word_is("for")) return parse_for();
        if (word_is("while")) return parse_while();
        if (word_is("case")) return parse_case();
        if (current_.kind == TokenKind::LeftBrace) return parse_brace();
        if (current_.kind == TokenKind::Word) {
            const auto text = source_.slice(current_.source);
            if (reserved(text)) {
                fail();
                return 0;
            }
            if (valid_name(text)) {
                const auto next = scan_embedded(source_, current_.next_offset);
                if (next.status == ScanStatus::Complete &&
                    next.token.kind == TokenKind::LeftParen) {
                    return parse_function();
                }
            }
        }
        return parse_simple();
    }

    [[nodiscard]] std::size_t parse_word(SyntaxKind kind,
                                          bool pattern = false) {
        if (current_.kind != TokenKind::Word) {
            fail();
            return 0;
        }
        const auto text = source_.slice(current_.source);
        if (!pattern && current_.has_unquoted_glob &&
            text != "[" && text != "]") {
            fail();
            return 0;
        }
        for (std::size_t i = 0; i < current_.fragments_required; ++i) {
            WordFragment fragment;
            const auto scanned = scan_embedded_fragment(
                source_, current_.source.offset, i, fragment);
            if (scanned.status != ScanStatus::Complete) {
                fail();
                return 0;
            }
            if (fragment.kind != FragmentKind::CommandSubstitution) {
                continue;
            }
            const auto inner_offset = fragment.source.offset + 2;
            const auto inner_length = fragment.source.length - 3;
            const auto nested = measure_embedded(SourceView{
                source_.slice(inner_offset, inner_length)});
            if (nested.status != ParseStatus::Complete) {
                status_ = nested.status;
                issue_ = location(source_, inner_offset +
                                            nested.issue.offset);
                return 0;
            }
            const auto nested_depth = depth_ + nested.required.context;
            if (nested_depth > sink_.counts.context) {
                sink_.counts.context = nested_depth;
            }
        }
        const auto token_id = sink_.counts.tokens - 1;
        const auto node_id = sink_.node(kind, current_.source, token_id);
        consume();
        return node_id;
    }

    [[nodiscard]] std::size_t parse_simple() {
        Frame frame{*this, SyntaxKind::Simple};
        const auto node_id = sink_.node(
            SyntaxKind::Simple, {current_.source.offset, 0});
        bool saw_word = false;
        bool command_word = false;
        while (status_ == ParseStatus::Complete &&
               current_.kind == TokenKind::Word) {
            const auto text = source_.slice(current_.source);
            const auto equal = text.find('=');
            const bool assignment = !command_word &&
                                    equal != std::string_view::npos &&
                                    valid_name(text.substr(0, equal));
            if (!assignment) command_word = true;
            const auto child = parse_word(assignment ? SyntaxKind::Assignment
                                                    : SyntaxKind::Word);
            if (status_ != ParseStatus::Complete) break;
            sink_.link(node_id, child);
            saw_word = true;
        }
        if (!saw_word && status_ == ParseStatus::Complete) fail();
        sink_.end(node_id, last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_brace() {
        Frame frame{*this, SyntaxKind::BraceGroup};
        const auto node_id = sink_.node(
            SyntaxKind::BraceGroup, {current_.source.offset, 0});
        if (!take(TokenKind::LeftBrace)) return node_id;
        const auto body = parse_list({.brace = true}, true);
        if (status_ == ParseStatus::Complete) sink_.link(node_id, body);
        if (take(TokenKind::RightBrace)) sink_.end(node_id, last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_function() {
        Frame frame{*this, SyntaxKind::Function};
        const auto node_id = sink_.node(
            SyntaxKind::Function, {current_.source.offset, 0});
        const auto name = parse_word(SyntaxKind::Word);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, name);
        if (!take(TokenKind::LeftParen) ||
            !take(TokenKind::RightParen)) return node_id;
        newlines();
        if (current_.kind != TokenKind::LeftBrace) {
            fail();
            return node_id;
        }
        const auto body = parse_brace();
        if (status_ == ParseStatus::Complete) {
            sink_.link(node_id, body);
            sink_.end(node_id, last_end_);
        }
        return node_id;
    }

    [[nodiscard]] std::size_t parse_if() {
        Frame frame{*this, SyntaxKind::If};
        const auto node_id = sink_.node(
            SyntaxKind::If, {current_.source.offset, 0});
        consume();
        const auto condition = parse_list({.first = "then"}, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, condition);
        if (!take_word("then")) return node_id;
        const auto body = parse_list({"elif", "else", "fi"}, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, body);
        while (word_is("elif")) {
            const auto branch = sink_.node(
                SyntaxKind::Elif, {current_.source.offset, 0});
            consume();
            const auto test = parse_list({.first = "then"}, true);
            if (status_ != ParseStatus::Complete) return node_id;
            sink_.link(branch, test);
            if (!take_word("then")) return node_id;
            const auto branch_body = parse_list({"elif", "else", "fi"}, true);
            if (status_ != ParseStatus::Complete) return node_id;
            sink_.link(branch, branch_body);
            sink_.end(branch, last_end_);
            sink_.link(node_id, branch);
        }
        if (word_is("else")) {
            const auto branch = sink_.node(
                SyntaxKind::Else, {current_.source.offset, 0});
            consume();
            const auto branch_body = parse_list({.first = "fi"}, true);
            if (status_ != ParseStatus::Complete) return node_id;
            sink_.link(branch, branch_body);
            sink_.end(branch, last_end_);
            sink_.link(node_id, branch);
        }
        if (take_word("fi")) sink_.end(node_id, last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_for() {
        Frame frame{*this, SyntaxKind::For};
        const auto node_id = sink_.node(
            SyntaxKind::For, {current_.source.offset, 0});
        consume();
        if (current_.kind != TokenKind::Word ||
            !valid_name(source_.slice(current_.source))) {
            fail();
            return node_id;
        }
        const auto name = parse_word(SyntaxKind::Word);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, name);
        if (word_is("in")) {
            sink_.mark_in_clause(node_id);
            consume();
            while (status_ == ParseStatus::Complete &&
                   current_.kind == TokenKind::Word) {
                const auto value = parse_word(SyntaxKind::Word);
                if (status_ != ParseStatus::Complete) return node_id;
                sink_.link(node_id, value);
            }
        }
        if (current_.kind != TokenKind::Semicolon &&
            current_.kind != TokenKind::Newline) {
            fail();
            return node_id;
        }
        consume();
        newlines();
        if (!take_word("do")) return node_id;
        const auto body = parse_list({.first = "done"}, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, body);
        if (take_word("done")) sink_.end(node_id, last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_while() {
        Frame frame{*this, SyntaxKind::While};
        const auto node_id = sink_.node(
            SyntaxKind::While, {current_.source.offset, 0});
        consume();
        const auto test = parse_list({.first = "do"}, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, test);
        if (!take_word("do")) return node_id;
        const auto body = parse_list({.first = "done"}, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, body);
        if (take_word("done")) sink_.end(node_id, last_end_);
        return node_id;
    }

    [[nodiscard]] std::size_t parse_case_item() {
        Frame frame{*this, SyntaxKind::CaseItem};
        const auto node_id = sink_.node(
            SyntaxKind::CaseItem, {current_.source.offset, 0});
        auto pattern = parse_word(SyntaxKind::Pattern, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, pattern);
        while (current_.kind == TokenKind::CaseBar) {
            consume();
            pattern = parse_word(SyntaxKind::Pattern, true);
            if (status_ != ParseStatus::Complete) return node_id;
            sink_.link(node_id, pattern, SyntaxJoin::Alternative);
        }
        if (!take(TokenKind::RightParen)) return node_id;
        const auto body = parse_list({.item = true});
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, body);
        if (take(TokenKind::DoubleSemicolon)) {
            sink_.end(node_id, last_end_);
        }
        return node_id;
    }

    [[nodiscard]] std::size_t parse_case() {
        Frame frame{*this, SyntaxKind::Case};
        const auto node_id = sink_.node(
            SyntaxKind::Case, {current_.source.offset, 0});
        consume();
        const auto selector = parse_word(SyntaxKind::Word, true);
        if (status_ != ParseStatus::Complete) return node_id;
        sink_.link(node_id, selector);
        if (!take_word("in")) return node_id;
        newlines();
        while (status_ == ParseStatus::Complete && !word_is("esac") &&
               current_.kind != TokenKind::End) {
            const auto item = parse_case_item();
            if (status_ != ParseStatus::Complete) return node_id;
            sink_.link(node_id, item);
            newlines();
        }
        if (take_word("esac")) sink_.end(node_id, last_end_);
        return node_id;
    }

    SourceView source_;
    Sink sink_;
    ScanToken current_;
    std::size_t offset_ = 0;
    std::size_t last_end_ = 0;
    std::size_t depth_ = 0;
    ParseStatus status_ = ParseStatus::Complete;
    SourceLocation issue_;
};

[[nodiscard]] OverflowInfo first_short(const ParseCounts& required,
                                        const ScriptStorage& storage) {
    if (storage.tokens.size() < required.tokens) {
        return {StorageClass::Tokens, required.tokens};
    }
    if (storage.fragments.size() < required.fragments) {
        return {StorageClass::WordFragments, required.fragments};
    }
    if (storage.nodes.size() < required.nodes) {
        return {StorageClass::SyntaxNodes, required.nodes};
    }
    if (storage.links.size() < required.links) {
        return {StorageClass::SyntaxLinks, required.links};
    }
    if (storage.context.size() < required.context) {
        return {StorageClass::ParserContext, required.context};
    }
    return {};
}

[[nodiscard]] bool sufficient(const ParseCounts& required,
                              const ScriptStorage& storage) {
    return storage.tokens.size() >= required.tokens &&
           storage.fragments.size() >= required.fragments &&
           storage.nodes.size() >= required.nodes &&
           storage.links.size() >= required.links &&
           storage.context.size() >= required.context;
}

}  // namespace

MeasureOutcome measure_embedded(SourceView source) {
    Parser parser{source, nullptr};
    return parser.run();
}

ParseOutcome parse_embedded(SourceView source, ScriptStorage storage,
                            EmbeddedScript& out) {
    const auto measured = measure_embedded(source);
    if (measured.status != ParseStatus::Complete) {
        return {measured.status, measured.required, measured.issue, {}};
    }
    if (!sufficient(measured.required, storage)) {
        return {ParseStatus::Overflow, measured.required, {},
                first_short(measured.required, storage)};
    }
    Parser parser{source, &storage};
    const auto built = parser.run();
    if (built.status != ParseStatus::Complete) {
        return {built.status, built.required, built.issue, {}};
    }
    out = {source,
           storage.tokens.first(built.required.tokens),
           storage.fragments.first(built.required.fragments),
           storage.nodes.first(built.required.nodes),
           storage.links.first(built.required.links),
           0};
    return {ParseStatus::Complete, built.required, {}, {}};
}

}  // namespace mm::shell
