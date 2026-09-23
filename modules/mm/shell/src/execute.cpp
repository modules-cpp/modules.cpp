// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :command;
import :execute;
import :expand;
import :fields;
import :source;
import :state;
import :status;
import :syntax;

namespace mm::shell {
namespace {

[[nodiscard]] const SyntaxLink* link_at(const EmbeddedScript& script,
                                        const SyntaxNode& node,
                                        std::size_t index) {
    if (index >= node.link_count) return nullptr;
    auto link = node.first_link;
    for (std::size_t i = 0; i < index; ++i) {
        if (link >= script.links.size()) return nullptr;
        link = script.links[link].next;
    }
    return link < script.links.size() ? &script.links[link] : nullptr;
}

[[nodiscard]] CommandResult failure(int status, Status error) {
    return {.flow = Flow::Normal, .status = status, .error = error};
}

}  // namespace

Status Evaluator::begin(const EmbeddedScript& script, Registry& registry,
                        CommandContext& context,
                        EvaluatorStorage storage) {
    active_ = false;
    if (script.root >= script.nodes.size() ||
        script.nodes[script.root].kind != SyntaxKind::Program ||
        script.nodes[script.root].link_count != 1) {
        return Status::BadArgument;
    }
    const auto* program_link = link_at(
        script, script.nodes[script.root], 0);
    if (program_link == nullptr ||
        program_link->child >= script.nodes.size() ||
        script.nodes[program_link->child].kind != SyntaxKind::List) {
        return Status::BadArgument;
    }
    script_ = &script;
    registry_ = &registry;
    context_ = &context;
    storage_ = storage;
    list_node_ = program_link->child;
    list_link_ = 0;
    andor_node_ = 0;
    command_link_ = 0;
    last_status_ = 0;
    active_ = true;
    return Status::Ok;
}

StepResult Evaluator::step(std::size_t operation_budget) {
    if (!active_) return {.step = Step::Complete};
    if (operation_budget == 0) return {.step = Step::Running};
    const auto& script = *script_;
    const auto& list = script.nodes[list_node_];
    for (std::size_t operation = 0; operation < operation_budget;
         ++operation) {
        if (andor_node_ == 0) {
            if (list_link_ >= list.link_count) {
                active_ = false;
                return {.step = Step::Complete,
                        .command = {.status = last_status_}};
            }
            const auto* list_entry = link_at(script, list, list_link_);
            if (list_entry == nullptr ||
                list_entry->child >= script.nodes.size() ||
                script.nodes[list_entry->child].kind != SyntaxKind::AndOr) {
                active_ = false;
                return {.step = Step::Failed,
                        .command = failure(2, Status::BadArgument)};
            }
            andor_node_ = list_entry->child + 1;
            command_link_ = 0;
        }
        const auto node_id = andor_node_ - 1;
        const auto& andor = script.nodes[node_id];
        if (command_link_ >= andor.link_count) {
            andor_node_ = 0;
            ++list_link_;
            continue;
        }
        const auto* command = link_at(script, andor, command_link_);
        if (command == nullptr || command->child >= script.nodes.size()) {
            active_ = false;
            return {.step = Step::Failed,
                    .command = failure(2, Status::BadArgument)};
        }
        if (command_link_ != 0 &&
            ((command->join == SyntaxJoin::And && last_status_ != 0) ||
             (command->join == SyntaxJoin::Or && last_status_ == 0))) {
            ++command_link_;
            continue;
        }

        const auto& syntax = script.nodes[command->child];
        bool negate = false;
        std::size_t simple_id = command->child;
        if (syntax.kind == SyntaxKind::Negation) {
            const auto* inner = link_at(script, syntax, 0);
            if (inner == nullptr || inner->child >= script.nodes.size()) {
                active_ = false;
                return {.step = Step::Failed,
                        .command = failure(2, Status::BadArgument)};
            }
            negate = true;
            simple_id = inner->child;
        }
        const auto& simple = script.nodes[simple_id];
        if (simple.kind != SyntaxKind::Simple) {
            active_ = false;
            return {.step = Step::Failed,
                    .command = failure(2, Status::Unsupported)};
        }

        std::size_t argument_count = 0;
        std::size_t text_used = 0;
        bool failed = false;
        CommandResult result{};
        for (std::size_t word_index = 0;
             word_index < simple.link_count; ++word_index) {
            const auto* word_link = link_at(script, simple, word_index);
            if (word_link == nullptr ||
                word_link->child >= script.nodes.size()) {
                failed = true;
                result = failure(2, Status::BadArgument);
                break;
            }
            const auto& word = script.nodes[word_link->child];
            if (word.kind != SyntaxKind::Word ||
                word.token_index >= script.tokens.size()) {
                failed = true;
                result = failure(2, Status::Unsupported);
                break;
            }
            const auto& token = script.tokens[word.token_index];
            if (token.fragment_first > script.fragments.size() ||
                token.fragment_count >
                    script.fragments.size() - token.fragment_first) {
                failed = true;
                result = failure(2, Status::BadArgument);
                break;
            }
            FieldView expanded;
            const auto outcome = expand_word(
                script.source,
                script.fragments.subspan(token.fragment_first,
                                         token.fragment_count),
                context_->state, storage_.expansion, expanded);
            if (outcome.status != Status::Ok) {
                failed = true;
                result = failure(1, outcome.status);
                result.overflow = outcome.overflow;
                break;
            }
            for (std::size_t field_index = 0;
                 field_index < expanded.fields.size(); ++field_index) {
                if (argument_count == storage_.arguments.size()) {
                    failed = true;
                    result = failure(2, Status::Overflow);
                    result.overflow = {
                        StorageClass::ExpandedFields, argument_count + 1};
                    break;
                }
                const auto value = expanded.field(field_index);
                if (text_used > storage_.argument_text.size() ||
                    value.size() >
                        storage_.argument_text.size() - text_used) {
                    failed = true;
                    result = failure(2, Status::Overflow);
                    result.overflow = {
                        StorageClass::ExpandedFieldText,
                        text_used + value.size()};
                    break;
                }
                if (value.empty()) {
                    storage_.arguments[argument_count++] = {};
                    continue;
                }
                for (std::size_t i = 0; i < value.size(); ++i) {
                    storage_.argument_text[text_used + i] = value[i];
                }
                storage_.arguments[argument_count++] = {
                    storage_.argument_text.data() + text_used,
                    value.size()};
                text_used += value.size();
            }
            if (failed) break;
        }
        if (!failed && argument_count != 0) {
            const auto* descriptor = registry_->find(
                storage_.arguments[0]);
            if (descriptor == nullptr) {
                result = failure(
                    static_cast<int>(CommandStatus::NotFound),
                    Status::NotFound);
            } else {
                result = dispatch(
                    *descriptor,
                    std::span<const std::string_view>{
                        storage_.arguments.data(), argument_count},
                    *context_);
            }
        }
        if (negate && !failed) {
            result.status = result.status == 0 ? 1 : 0;
        }
        if (result.flow == Flow::Break || result.flow == Flow::Continue ||
            result.flow == Flow::Return || result.flow == Flow::Replace) {
            result = failure(2, Status::BadArgument);
        }
        last_status_ = result.status;
        context_->state.last_status = last_status_;
        ++command_link_;
        if (result.flow == Flow::Exit) {
            active_ = false;
            return {.step = Step::Complete, .command = result};
        }
        if (result.flow == Flow::Yield) {
            return {.step = Step::Yielded, .command = result};
        }
        return {.step = Step::Running, .command = result};
    }
    return {.step = Step::Running};
}

}  // namespace mm::shell
