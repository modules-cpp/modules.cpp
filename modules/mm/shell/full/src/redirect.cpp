// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

module mm.shell.full;

import :redirect;
import :syntax;

namespace mm::shell::full {
namespace {

[[nodiscard]] RedirectionKind classify(TokenKind kind) {
    switch (kind) {
        case TokenKind::Output: return RedirectionKind::Truncate;
        case TokenKind::Append: return RedirectionKind::Append;
        case TokenKind::HereDocument:
            return RedirectionKind::HereDocument;
        case TokenKind::DuplicateInput:
            return RedirectionKind::DuplicateInput;
        case TokenKind::DuplicateOutput:
            return RedirectionKind::DuplicateOutput;
        default: return RedirectionKind::Input;
    }
}

[[nodiscard]] bool is_input(RedirectionKind kind) {
    return kind == RedirectionKind::Input ||
           kind == RedirectionKind::HereDocument ||
           kind == RedirectionKind::DuplicateInput;
}

}  // namespace

RedirectionPlan plan_redirections(
    const FullScript& script, std::size_t command_node) {
    RedirectionPlan plan;
    if (command_node >= script.nodes.size()) {
        plan.diagnostic = {ParseStatus::Malformed, 0,
                           "invalid command node"};
        return plan;
    }
    for (const auto child : script.nodes[command_node].children) {
        if (child.node >= script.nodes.size()) {
            plan.diagnostic = {ParseStatus::Malformed, 0,
                               "invalid redirection node"};
            return plan;
        }
        const auto& node = script.nodes[child.node];
        if (node.kind != NodeKind::Redirection) continue;
        auto operator_at = node.first_token;
        if (operator_at >= script.tokens.size()) {
            plan.diagnostic = {ParseStatus::Malformed, 0,
                               "invalid redirection token"};
            return plan;
        }
        const bool numbered =
            script.tokens[operator_at].kind == TokenKind::IoNumber;
        if (numbered) ++operator_at;
        if (operator_at + 1 >= script.tokens.size()) {
            plan.diagnostic = {ParseStatus::Malformed, 0,
                               "incomplete redirection"};
            return plan;
        }
        const auto kind = classify(script.tokens[operator_at].kind);
        RedirectionStep step;
        step.kind = kind;
        step.target = is_input(kind) ? 0U : 1U;
        if (numbered) {
            const auto text = script.text(
                script.tokens[node.first_token].source);
            const auto converted = std::from_chars(
                text.data(), text.data() + text.size(), step.target);
            if (converted.ec != std::errc{} ||
                converted.ptr != text.data() + text.size()) {
                plan.diagnostic = {
                    ParseStatus::Malformed,
                    script.tokens[node.first_token].source.offset,
                    "invalid IO number"};
                return plan;
            }
        }
        step.operand.assign(script.text(
            script.tokens[operator_at + 1].source));
        if (kind == RedirectionKind::HereDocument) {
            bool found = false;
            for (const auto& document : script.documents) {
                if (document.operator_token != operator_at) continue;
                step.body.assign(script.text(document.body));
                step.expand_body = document.expand;
                found = true;
                break;
            }
            if (!found) {
                plan.diagnostic = {
                    ParseStatus::Malformed,
                    script.tokens[operator_at].source.offset,
                    "missing here-document body"};
                return plan;
            }
        }
        plan.steps.push_back(std::move(step));
    }
    return plan;
}

}  // namespace mm::shell::full
