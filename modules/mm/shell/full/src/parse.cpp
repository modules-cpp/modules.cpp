// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

module mm.shell.full;

import :parse;
import :scan;
import :syntax;

namespace mm::shell::full {
namespace {

[[nodiscard]] bool redirection(TokenKind kind) {
    return kind == TokenKind::Input || kind == TokenKind::Output ||
           kind == TokenKind::Append ||
           kind == TokenKind::HereDocument ||
           kind == TokenKind::DuplicateInput ||
           kind == TokenKind::DuplicateOutput;
}

[[nodiscard]] bool separator(TokenKind kind) {
    return kind == TokenKind::Newline ||
           kind == TokenKind::Semicolon;
}

struct Parser {
    FullScript& script;
    std::size_t at = 0;
    std::size_t depth = 0;
    Diagnostic diagnostic;

    [[nodiscard]] TokenKind kind() const {
        return script.tokens[at].kind;
    }

    [[nodiscard]] bool word(std::string_view value) const {
        return kind() == TokenKind::Word &&
               !script.tokens[at].quoted &&
               script.text(script.tokens[at].source) == value;
    }

    [[nodiscard]] bool stop(
        std::initializer_list<std::string_view> words,
        TokenKind delimiter) const {
        if (kind() == TokenKind::End || kind() == delimiter) return true;
        for (const auto value : words) {
            if (word(value)) return true;
        }
        return false;
    }

    void fail(ParseStatus status, std::string_view message) {
        if (diagnostic.status != ParseStatus::Complete) return;
        diagnostic = {status, script.tokens[at].source.offset,
                      std::string(message)};
    }

    [[nodiscard]] std::size_t add(NodeKind type, std::size_t first) {
        const auto index = script.nodes.size();
        script.nodes.push_back({.kind = type, .first_token = first});
        return index;
    }

    void child(std::size_t parent, std::size_t index,
               Join join = Join::Sequence) {
        script.nodes[parent].children.push_back({index, join});
    }

    void finish(std::size_t index) {
        script.nodes[index].last_token = at;
    }

    void skip_separators() {
        while (separator(kind())) ++at;
    }

    void skip_newlines() {
        while (kind() == TokenKind::Newline) ++at;
    }

    [[nodiscard]] bool require_word(std::string_view value) {
        if (!word(value)) {
            fail(ParseStatus::Malformed, "expected shell keyword");
            return false;
        }
        ++at;
        return true;
    }

    [[nodiscard]] std::size_t parse_redirection() {
        const auto first = at;
        const auto node = add(NodeKind::Redirection, first);
        if (kind() == TokenKind::IoNumber) ++at;
        if (!redirection(kind())) {
            fail(ParseStatus::Malformed, "expected redirection operator");
            return node;
        }
        ++at;
        if (kind() != TokenKind::Word) {
            fail(ParseStatus::Malformed, "redirection requires one word");
            return node;
        }
        ++at;
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_simple() {
        const auto first = at;
        const auto node = add(NodeKind::Simple, first);
        bool saw_command = false;
        while (kind() == TokenKind::Word ||
               kind() == TokenKind::IoNumber ||
               redirection(kind())) {
            if (redirection(kind()) ||
                kind() == TokenKind::IoNumber) {
                child(node, parse_redirection());
            } else {
                const auto spelling = script.text(
                    script.tokens[at].source);
                if (!saw_command && spelling.find('=') ==
                    std::string_view::npos) {
                    saw_command = true;
                    if (!script.tokens[at].quoted &&
                        (spelling == "eval" || spelling == "." ||
                         spelling == "alias" || spelling == "jobs" ||
                         spelling == "source" ||
                         spelling == "function")) {
                        fail(ParseStatus::Unsupported,
                             "excluded shell command");
                    }
                }
                ++at;
            }
        }
        if (at == first) {
            fail(ParseStatus::Malformed, "expected command");
        }
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_group(bool subshell) {
        const auto first = at++;
        const auto node = add(subshell ? NodeKind::Subshell
                                      : NodeKind::BraceGroup, first);
        const auto close = subshell ? TokenKind::CloseParen
                                    : TokenKind::CloseBrace;
        const auto body = parse_list({}, close);
        child(node, body);
        if (script.nodes[body].children.empty()) {
            fail(ParseStatus::Malformed, "empty shell group");
        }
        if (kind() != close) {
            fail(ParseStatus::Incomplete, "unclosed group");
        } else {
            ++at;
        }
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_if() {
        const auto node = add(NodeKind::If, at++);
        const auto condition = parse_list({"then"}, TokenKind::End);
        child(node, condition);
        if (script.nodes[condition].children.empty()) {
            fail(ParseStatus::Malformed, "if needs a condition");
        }
        if (!require_word("then")) return node;
        const auto then_body = parse_list({"elif", "else", "fi"},
                                          TokenKind::End);
        child(node, then_body);
        if (script.nodes[then_body].children.empty()) {
            fail(ParseStatus::Malformed, "then needs a command");
        }
        while (word("elif")) {
            ++at;
            const auto elif_condition = parse_list({"then"},
                                                    TokenKind::End);
            child(node, elif_condition, Join::Alternative);
            if (script.nodes[elif_condition].children.empty()) {
                fail(ParseStatus::Malformed, "elif needs a condition");
            }
            if (!require_word("then")) return node;
            const auto elif_body = parse_list({"elif", "else", "fi"},
                                              TokenKind::End);
            child(node, elif_body);
            if (script.nodes[elif_body].children.empty()) {
                fail(ParseStatus::Malformed, "then needs a command");
            }
        }
        if (word("else")) {
            ++at;
            const auto else_body = parse_list({"fi"}, TokenKind::End);
            child(node, else_body, Join::Alternative);
            if (script.nodes[else_body].children.empty()) {
                fail(ParseStatus::Malformed, "else needs a command");
            }
        }
        if (!require_word("fi")) return node;
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_while() {
        const auto node = add(NodeKind::While, at++);
        const auto condition = parse_list({"do"}, TokenKind::End);
        child(node, condition);
        if (script.nodes[condition].children.empty()) {
            fail(ParseStatus::Malformed, "loop needs a condition");
        }
        if (!require_word("do")) return node;
        const auto body = parse_list({"done"}, TokenKind::End);
        child(node, body);
        if (script.nodes[body].children.empty()) {
            fail(ParseStatus::Malformed, "loop needs a body");
        }
        if (!require_word("done")) return node;
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_for() {
        const auto node = add(NodeKind::For, at++);
        if (kind() != TokenKind::Word) {
            fail(ParseStatus::Malformed, "for requires a name");
            return node;
        }
        ++at;
        if (word("in")) {
            ++at;
            while (kind() == TokenKind::Word) ++at;
        }
        skip_separators();
        if (!require_word("do")) return node;
        const auto body = parse_list({"done"}, TokenKind::End);
        child(node, body);
        if (script.nodes[body].children.empty()) {
            fail(ParseStatus::Malformed, "for needs a body");
        }
        if (!require_word("done")) return node;
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_case() {
        const auto node = add(NodeKind::Case, at++);
        if (kind() != TokenKind::Word) {
            fail(ParseStatus::Malformed, "case requires a selector");
            return node;
        }
        ++at;
        skip_newlines();
        if (!require_word("in")) return node;
        skip_separators();
        while (!word("esac") && kind() != TokenKind::End &&
               diagnostic.status == ParseStatus::Complete) {
            const auto item = add(NodeKind::CaseItem, at);
            if (kind() == TokenKind::OpenParen) ++at;
            bool pattern = false;
            while (kind() == TokenKind::Word ||
                   kind() == TokenKind::Pipe) {
                if (kind() == TokenKind::Word) pattern = true;
                ++at;
            }
            if (!pattern || kind() != TokenKind::CloseParen) {
                fail(ParseStatus::Malformed, "case pattern needs )");
                return node;
            }
            ++at;
            child(item, parse_list({"esac"},
                                   TokenKind::DoubleSemicolon));
            finish(item);
            child(node, item, Join::Alternative);
            if (kind() == TokenKind::DoubleSemicolon) ++at;
            skip_separators();
        }
        if (!require_word("esac")) return node;
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_command() {
        if (++depth > 256) {
            fail(ParseStatus::Malformed, "compound nesting exceeds limit");
            --depth;
            return add(NodeKind::Simple, at);
        }
        std::size_t node = 0;
        if (kind() == TokenKind::OpenParen) {
            node = parse_group(true);
        } else if (kind() == TokenKind::OpenBrace) {
            node = parse_group(false);
        } else if (word("if")) {
            node = parse_if();
        } else if (word("while") || word("until")) {
            node = parse_while();
        } else if (word("for")) {
            node = parse_for();
        } else if (word("case")) {
            node = parse_case();
        } else if (kind() == TokenKind::Word &&
                   at + 2 < script.tokens.size() &&
                   script.tokens[at + 1].kind ==
                       TokenKind::OpenParen &&
                   script.tokens[at + 2].kind ==
                       TokenKind::CloseParen) {
            node = add(NodeKind::Function, at);
            at += 3;
            skip_newlines();
            child(node, parse_command());
            finish(node);
        } else {
            node = parse_simple();
        }
        while ((redirection(kind()) ||
                kind() == TokenKind::IoNumber) &&
               diagnostic.status == ParseStatus::Complete) {
            child(node, parse_redirection());
            finish(node);
        }
        --depth;
        return node;
    }

    [[nodiscard]] std::size_t parse_pipeline() {
        const auto first = at;
        const bool negate = word("!");
        if (negate) ++at;
        const auto pipeline = add(NodeKind::Pipeline, first);
        child(pipeline, parse_command());
        while (kind() == TokenKind::Pipe &&
               diagnostic.status == ParseStatus::Complete) {
            ++at;
            skip_newlines();
            if (kind() == TokenKind::End) {
                fail(ParseStatus::Incomplete,
                     "pipeline requires another command");
                break;
            }
            child(pipeline, parse_command(), Join::Pipe);
        }
        finish(pipeline);
        if (!negate) return pipeline;
        const auto node = add(NodeKind::Negation, first);
        child(node, pipeline);
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_and_or() {
        const auto node = add(NodeKind::AndOr, at);
        child(node, parse_pipeline());
        while ((kind() == TokenKind::AndIf ||
                kind() == TokenKind::OrIf) &&
               diagnostic.status == ParseStatus::Complete) {
            const auto join = kind() == TokenKind::AndIf
                ? Join::And : Join::Or;
            ++at;
            skip_newlines();
            if (kind() == TokenKind::End) {
                fail(ParseStatus::Incomplete,
                     "and-or requires another command");
                break;
            }
            child(node, parse_pipeline(), join);
        }
        finish(node);
        return node;
    }

    [[nodiscard]] std::size_t parse_list(
        std::initializer_list<std::string_view> words,
        TokenKind delimiter) {
        const auto node = add(NodeKind::List, at);
        skip_separators();
        while (!stop(words, delimiter) &&
               diagnostic.status == ParseStatus::Complete) {
            const auto before = at;
            child(node, parse_and_or());
            if (at == before) {
                fail(ParseStatus::Malformed, "expected command");
                break;
            }
            if (!separator(kind()) && !stop(words, delimiter)) {
                fail(ParseStatus::Malformed,
                     "commands require a separator");
                break;
            }
            skip_separators();
        }
        finish(node);
        return node;
    }

    void run() {
        const auto program = add(NodeKind::Program, 0);
        child(program, parse_list({}, TokenKind::End));
        if (kind() != TokenKind::End &&
            diagnostic.status == ParseStatus::Complete) {
            fail(ParseStatus::Malformed, "unexpected trailing syntax");
        }
        finish(program);
        script.root = program;
    }
};

}  // namespace

ParseResult parse_full(std::string_view text) {
    ParseResult result;
    result.diagnostic = scan_full(text, result.script);
    if (result.diagnostic.status == ParseStatus::Complete) {
        Parser parser{result.script};
        parser.run();
        result.diagnostic = parser.diagnostic;
    }
    return result;
}

}  // namespace mm::shell::full
