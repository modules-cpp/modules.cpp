// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :function;
import :interpret;
import :parse;
import :redirect;
import :service;
import :state;
import :syntax;
import :trap;
import :word;
import mm.shell;

namespace mm::shell::full {

RunOutcome Interpreter::run_text(std::string_view text) {
    auto parsed = parse_full(text);
    if (!parsed.ok()) {
        return {.status = 2, .diagnostic = parsed.diagnostic};
    }
    return run(parsed.script);
}

RunOutcome Interpreter::run(const FullScript& script) {
    RunOutcome outcome;
    if (script.root >= script.nodes.size()) {
        outcome.status = 2;
        outcome.diagnostic = {ParseStatus::Malformed, 0, "empty program"};
        return outcome;
    }
    const auto& program = script.nodes[script.root];
    Step step;
    for (const auto& child : program.children) {
        step = run_node(script, child.node);
        if (step.flow != Flow::Normal || !step.ok()) break;
    }
    outcome.status = step.status;
    outcome.service = step.service;
    outcome.diagnostic = step.diagnostic;
    outcome.exited = step.flow == Flow::Exit;

    // The exit trap fires once, after the script body, and its own failure
    // does not replace the status the script reached.
    const auto fired = fire_traps(outcome.status);
    if (!fired.ok() && outcome.ok()) {
        outcome.service = fired.service;
        outcome.diagnostic = fired.diagnostic;
    }
    return outcome;
}

Interpreter::Step Interpreter::fire_traps(int status) {
    const auto action = state_.trap(exit_condition);
    if (action.empty()) return {};
    // Clearing first keeps a trap action that itself exits from recursing.
    const std::string command{action};
    state_.clear_trap(exit_condition);
    state_.core().last_status = status;
    auto parsed = parse_full(command);
    if (!parsed.ok()) {
        return {.status = 2, .diagnostic = parsed.diagnostic};
    }
    Step step;
    const auto& program = parsed.script.nodes[parsed.script.root];
    for (const auto& child : program.children) {
        step = run_node(parsed.script, child.node);
        if (step.flow == Flow::Exit || !step.ok()) break;
    }
    return step;
}

Interpreter::Step Interpreter::poll_signals() {
    if (services_.signal.poll == nullptr) return {};
    int condition = 0;
    if (services_.signal.poll(services_.signal.context, condition) !=
        ServiceStatus::Ok) {
        return {};
    }
    if (condition == 0) return {};
    const auto action = state_.trap(condition);
    if (action.empty()) return {};
    const std::string command{action};
    auto parsed = parse_full(command);
    if (!parsed.ok()) {
        return {.status = 2, .diagnostic = parsed.diagnostic};
    }
    Step step;
    const auto& program = parsed.script.nodes[parsed.script.root];
    for (const auto& child : program.children) {
        step = run_node(parsed.script, child.node);
        if (step.flow != Flow::Normal || !step.ok()) break;
    }
    return step;
}

Interpreter::Step Interpreter::run_node(const FullScript& script,
                                        std::size_t node) {
    if (node >= script.nodes.size() || depth_ > 64) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "node depth"}};
    }
    switch (script.nodes[node].kind) {
        case NodeKind::List: return run_list(script, node);
        case NodeKind::AndOr: return run_and_or(script, node, false);
        case NodeKind::Pipeline:
            return run_pipeline_node(script, node, false);
        case NodeKind::Simple: return run_simple(script, node, false);
        case NodeKind::If: return run_if(script, node);
        case NodeKind::While: return run_while(script, node);
        case NodeKind::For: return run_for(script, node);
        case NodeKind::Case: return run_case(script, node);
        case NodeKind::Subshell: return run_subshell(script, node);
        case NodeKind::BraceGroup: return run_list(script, node);
        case NodeKind::Function: return define(script, node);
        case NodeKind::Negation: {
            if (script.nodes[node].children.empty()) {
                return {.status = 2,
                        .diagnostic = {ParseStatus::Malformed, 0,
                                       "empty negation"}};
            }
            ++tested_;
            auto step = run_node(script,
                                 script.nodes[node].children[0].node);
            --tested_;
            if (step.flow != Flow::Normal || !step.ok()) return step;
            step.status = step.status == 0 ? 1 : 0;
            state_.core().last_status = step.status;
            return step;
        }
        default:
            return {.status = 2,
                    .diagnostic = {ParseStatus::Unsupported, 0,
                                   "unsupported construct"}};
    }
}

Interpreter::Step Interpreter::run_list(const FullScript& script,
                                        std::size_t node) {
    Step step;
    for (const auto& child : script.nodes[node].children) {
        step = run_node(script, child.node);
        if (step.flow != Flow::Normal || !step.ok()) return step;
        const auto signalled = poll_signals();
        if (!signalled.ok() || signalled.flow == Flow::Exit) return signalled;
    }
    return step;
}

Interpreter::Step Interpreter::run_and_or(const FullScript& script,
                                          std::size_t node, bool tested) {
    (void)tested;
    const auto& children = script.nodes[node].children;
    Step step;
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (i != 0) {
            const auto join = children[i].join;
            if (join == Join::And && step.status != 0) continue;
            if (join == Join::Or && step.status == 0) continue;
        }
        // Every operand but the last is a tested context, which is what makes
        // false || recovered exempt from set -e.
        const auto last = i + 1 == children.size();
        if (!last) ++tested_;
        step = run_node(script, children[i].node);
        if (!last) --tested_;
        if (step.flow != Flow::Normal || !step.ok()) return step;
    }
    return step;
}

Interpreter::Step Interpreter::run_if(const FullScript& script,
                                      std::size_t node) {
    const auto& children = script.nodes[node].children;
    // Children are condition, body, then alternating condition and body for
    // each elif, with a trailing body for else.
    std::size_t at = 0;
    while (at + 1 < children.size()) {
        ++tested_;
        auto condition = run_node(script, children[at].node);
        --tested_;
        if (condition.flow != Flow::Normal || !condition.ok()) {
            return condition;
        }
        if (condition.status == 0) {
            return run_node(script, children[at + 1].node);
        }
        at += 2;
    }
    if (at < children.size()) {
        return run_node(script, children[at].node);
    }
    Step step;
    state_.core().last_status = 0;
    return step;
}

Interpreter::Step Interpreter::run_while(const FullScript& script,
                                         std::size_t node) {
    const auto& children = script.nodes[node].children;
    if (children.size() != 2) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "while shape"}};
    }
    Step step;
    for (;;) {
        ++tested_;
        auto condition = run_node(script, children[0].node);
        --tested_;
        if (condition.flow != Flow::Normal || !condition.ok()) {
            return condition;
        }
        if (condition.status != 0) return step;
        auto body = run_node(script, children[1].node);
        if (!body.ok()) return body;
        if (body.flow == Flow::Break) {
            if (body.levels > 1) {
                body.levels -= 1;
                return body;
            }
            step.status = body.status;
            return step;
        }
        if (body.flow == Flow::Continue) {
            if (body.levels > 1) {
                body.levels -= 1;
                return body;
            }
            step.status = body.status;
            continue;
        }
        if (body.flow != Flow::Normal) return body;
        step = body;
    }
}

Interpreter::Step Interpreter::run_for(const FullScript& script,
                                       std::size_t node) {
    const auto& loop = script.nodes[node];
    if (loop.children.empty() ||
        loop.first_token + 1 >= script.tokens.size()) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "for shape"}};
    }
    // The parser keeps only the body as a child; the name and the word
    // list are tokens after the for keyword.
    const auto name = script.text(script.tokens[loop.first_token + 1].source);
    std::vector<std::string> items;
    Step failure;
    auto had_in_clause = false;
    auto token = loop.first_token + 2;
    if (token < script.tokens.size() &&
        script.tokens[token].kind == TokenKind::Word &&
        script.text(script.tokens[token].source) == "in") {
        had_in_clause = true;
        ++token;
        while (token < script.tokens.size() &&
               script.tokens[token].kind == TokenKind::Word) {
            if (!expand_fields(script, token, true, true, items, failure)) {
                return failure;
            }
            ++token;
        }
    }
    if (!had_in_clause) {
        for (std::size_t i = 1; i <= state_.core().argument_count(); ++i) {
            items.emplace_back(state_.core().positional(i).value);
        }
    }
    Step step;
    for (const auto& item : items) {
        if (!state_.core().assign(name, item).ok()) {
            return {.status = 1,
                    .diagnostic = {ParseStatus::Malformed, 0,
                                   "loop variable capacity"}};
        }
        auto body = run_node(script, loop.children.back().node);
        if (!body.ok()) return body;
        if (body.flow == Flow::Break) {
            if (body.levels > 1) {
                body.levels -= 1;
                return body;
            }
            step.status = body.status;
            return step;
        }
        if (body.flow == Flow::Continue) {
            if (body.levels > 1) {
                body.levels -= 1;
                return body;
            }
            step.status = body.status;
            continue;
        }
        if (body.flow != Flow::Normal) return body;
        step = body;
    }
    return step;
}

Interpreter::Step Interpreter::run_case(const FullScript& script,
                                        std::size_t node) {
    const auto& selection = script.nodes[node];
    if (selection.first_token + 1 >= script.tokens.size()) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "case shape"}};
    }
    std::vector<std::string> selector;
    Step failure;
    if (!expand_fields(script, selection.first_token + 1, false, false,
                       selector, failure)) {
        return failure;
    }
    const auto value = selector.empty() ? std::string{} : selector[0];
    for (const auto& entry : selection.children) {
        const auto& item = script.nodes[entry.node];
        if (item.kind != NodeKind::CaseItem || item.children.empty()) {
            continue;
        }
        // Patterns are the word tokens of the item before its body list.
        const auto body = item.children.back().node;
        const auto body_first = script.nodes[body].first_token;
        for (auto token = item.first_token;
             token < body_first && token < script.tokens.size(); ++token) {
            if (script.tokens[token].kind != TokenKind::Word) continue;
            std::vector<std::string> pattern;
            if (!expand_fields(script, token, false, false, pattern,
                               failure)) {
                return failure;
            }
            const auto text = pattern.empty() ? std::string{} : pattern[0];
            std::vector<PatternByte> bytes;
            bytes.reserve(text.size());
            for (const char c : text) bytes.push_back({c, false});
            const auto matched = match_case_pattern(bytes, value);
            if (matched.status != Status::Ok) {
                return {.status = 2,
                        .diagnostic = {ParseStatus::Malformed, 0,
                                       "case pattern"}};
            }
            if (!matched.matched) continue;
            state_.core().last_status = 0;
            return run_node(script, body);
        }
    }
    Step step;
    state_.core().last_status = 0;
    return step;
}

// A subshell forks the value state and its host metadata, so its assignments,
// its working directory, and its traps do not reach the parent. Its external
// side effects are real, exactly as a real subshell's are.
Interpreter::Step Interpreter::run_subshell(const FullScript& script,
                                            std::size_t node) {
    const auto& children = script.nodes[node].children;
    if (children.empty()) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "subshell shape"}};
    }
    FullState child;
    if (state_.fork_into(child) != Status::Ok) {
        return {.status = 1,
                .diagnostic = {ParseStatus::Malformed, 0,
                               "subshell state capacity"}};
    }
    Interpreter nested{child, services_};
    nested.depth_ = depth_ + 1;
    nested.streams_ = streams_;
    (void)nested.functions_.adopt_from(functions_);
    auto step = nested.run_node(script, children[0].node);
    spawns_ += nested.spawns_;
    for (const auto& record : nested.externals_) {
        externals_.push_back(record);
    }
    // Exit inside a subshell ends only the subshell.
    if (step.flow == Flow::Exit) step.flow = Flow::Normal;
    state_.core().last_status = step.status;
    return step;
}

Interpreter::Step Interpreter::define(const FullScript& script,
                                      std::size_t node) {
    const auto& definition = script.nodes[node];
    if (definition.children.size() != 1) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "function shape"}};
    }
    // The name is the token the definition starts on; the one child is the
    // body command, whose token span is the text the library copies.
    const auto name = script.text(
        script.tokens[definition.first_token].source);
    const auto& body = script.nodes[definition.children[0].node];
    if (body.last_token >= script.tokens.size()) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "function body"}};
    }
    const auto first = script.tokens[body.first_token].source;
    const auto last = script.tokens[body.last_token].source;
    const auto start = first.offset;
    const auto end = last.offset + last.length;
    // The body text is copied as written, braces included. A brace group is a
    // valid script on its own, so nothing has to be trimmed and no token
    // boundary can be miscounted.
    const auto text = std::string{script.text({start, end - start})};
    const auto defined = functions_.define(name, text);
    if (!defined.ok()) {
        return {.status = 1, .diagnostic = defined.diagnostic};
    }
    Step step;
    state_.core().last_status = 0;
    return step;
}

}  // namespace mm::shell::full
