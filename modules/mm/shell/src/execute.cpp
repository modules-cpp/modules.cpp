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
import :io;
import :pattern;
import :source;
import :state;
import :status;
import :syntax;
import :word;

namespace mm::shell {
namespace {

constexpr std::size_t no_node = static_cast<std::size_t>(-1);

// One work unit covers at most sixteen bytes of resumable evaluator copying.
constexpr std::size_t bytes_per_unit = 16;

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

[[nodiscard]] std::size_t child_at(const EmbeddedScript& script,
                                   const SyntaxNode& node,
                                   std::size_t index) {
    const auto* link = link_at(script, node, index);
    if (link == nullptr || link->child >= script.nodes.size()) {
        return no_node;
    }
    return link->child;
}

[[nodiscard]] std::size_t last_child(const EmbeddedScript& script,
                                     const SyntaxNode& node) {
    if (node.link_count == 0 || node.last_link >= script.links.size()) {
        return no_node;
    }
    const auto child = script.links[node.last_link].child;
    return child < script.nodes.size() ? child : no_node;
}

[[nodiscard]] std::size_t list_child(const EmbeddedScript& script,
                                     const SyntaxNode& node,
                                     std::size_t index) {
    const auto child = child_at(script, node, index);
    if (child == no_node || script.nodes[child].kind != SyntaxKind::List) {
        return no_node;
    }
    return child;
}

[[nodiscard]] CommandResult failure(int status, Status error) {
    return {.flow = Flow::Normal, .status = status, .error = error};
}

[[nodiscard]] std::span<const WordFragment> word_fragments(
    const EmbeddedScript& script, std::size_t node_id) {
    if (node_id >= script.nodes.size()) return {};
    const auto& node = script.nodes[node_id];
    if (node.token_index >= script.tokens.size()) return {};
    const auto& token = script.tokens[node.token_index];
    if (token.fragment_first > script.fragments.size() ||
        token.fragment_count >
            script.fragments.size() - token.fragment_first) {
        return {};
    }
    return script.fragments.subspan(token.fragment_first,
                                    token.fragment_count);
}

[[nodiscard]] std::string_view word_text(const EmbeddedScript& script,
                                         std::size_t node_id) {
    if (node_id >= script.nodes.size()) return {};
    const auto& node = script.nodes[node_id];
    if (node.token_index >= script.tokens.size()) return {};
    return script.source.slice(script.tokens[node.token_index].source);
}

[[nodiscard]] std::string_view single_field(const FieldView& view) {
    return view.fields.empty() ? std::string_view{} : view.field(0);
}

}  // namespace

Status Evaluator::push(FrameKind kind, std::size_t node, bool tested,
                       bool negate) {
    if (node >= script_->nodes.size()) return Status::BadArgument;
    if (frame_count_ == storage_.frames.size()) return Status::Overflow;
    storage_.frames[frame_count_] = EvaluatorFrame{
        .kind = kind,
        .node = node,
        .item_mark = items_used_,
        .text_mark = item_text_used_,
        .negate = negate,
        .tested = tested,
    };
    ++frame_count_;
    return Status::Ok;
}

void Evaluator::pop() {
    if (frame_count_ == 0) return;
    const auto& frame = storage_.frames[frame_count_ - 1];
    items_used_ = frame.item_mark;
    item_text_used_ = frame.text_mark;
    --frame_count_;
}

void Evaluator::complete_frame(int status) {
    if (frame_count_ == 0) return;
    const auto negate = storage_.frames[frame_count_ - 1].negate;
    last_status_ = negate ? (status == 0 ? 1 : 0) : status;
    context_->state.last_status = last_status_;
    pop();
}

IoServices Evaluator::handler_io() {
    return IoServices{
        storage_.staged_output.empty() ? context_->io.out
                                       : staging_out_.sink(),
        storage_.staged_error.empty() ? context_->io.err
                                     : staging_error_.sink(),
    };
}

bool Evaluator::pending_output() const {
    return staging_out_.written > drained_out_ ||
           staging_error_.written > drained_error_;
}

// One bounded advance per call: the whole undrained remainder of one stream.
// WouldBlock consumes no byte and is retried on a later step.
Evaluator::Drain Evaluator::drain_once() {
    struct Stream {
        MemorySink& sink;
        std::size_t& drained;
        const ByteSink& target;
    };
    const Stream streams[]{
        {staging_out_, drained_out_, context_->io.out},
        {staging_error_, drained_error_, context_->io.err},
    };
    for (const auto& stream : streams) {
        if (stream.sink.written <= stream.drained) continue;
        const auto remainder =
            stream.sink.view().substr(stream.drained);
        const auto written = stream.target.write(remainder);
        if (written == SinkResult::WouldBlock) return Drain::Blocked;
        if (written == SinkResult::Failed) {
            drain_failure_ = stream.target.failure();
            return Drain::Failed;
        }
        stream.drained = stream.sink.written;
        return Drain::Advanced;
    }
    return Drain::Done;
}

void Evaluator::recycle_staging() {
    staging_out_.reset();
    staging_error_.reset();
    drained_out_ = 0;
    drained_error_ = 0;
}

Evaluator::Outcome Evaluator::deliver(StepResult result) {
    if (!pending_output()) {
        return Outcome{.returns = true, .result = result};
    }
    // The completed result is retained until its bytes reach the application,
    // so a partial transport write never re-enters the handler.
    held_ = result;
    holding_ = true;
    return Outcome{.returns = true, .result = {.step = Step::Running}};
}

bool Evaluator::unwind_to_loop(bool pop_loop) {
    auto index = frame_count_;
    while (index != 0) {
        const auto kind = storage_.frames[index - 1].kind;
        if (kind == FrameKind::While || kind == FrameKind::For) break;
        --index;
    }
    if (index == 0) return false;
    while (frame_count_ > index) pop();
    if (pop_loop) {
        complete_frame(last_status_);
        return true;
    }
    auto& frame = storage_.frames[index - 1];
    frame.status = last_status_;
    frame.phase = frame.kind == FrameKind::While ? 0 : 1;
    return true;
}

Status Evaluator::append_item(std::string_view value) {
    if (items_used_ == storage_.loop_items.size()) return Status::Overflow;
    if (value.size() > storage_.loop_text.size() - item_text_used_) {
        return Status::Overflow;
    }
    if (value.empty()) {
        storage_.loop_items[items_used_++] = {};
        return Status::Ok;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        storage_.loop_text[item_text_used_ + i] = value[i];
    }
    storage_.loop_items[items_used_++] = {
        storage_.loop_text.data() + item_text_used_, value.size()};
    item_text_used_ += value.size();
    return Status::Ok;
}

Status Evaluator::build_pattern(std::size_t node, std::size_t& length) {
    const auto& script = *script_;
    length = 0;
    const auto fragments = word_fragments(script, node);
    if (fragments.empty() &&
        (node >= script.nodes.size() ||
         script.nodes[node].kind != SyntaxKind::Pattern)) {
        return Status::BadArgument;
    }
    const auto append = [&](std::string_view text, bool quoted) {
        if (text.size() > storage_.pattern.size() - length) {
            return Status::Overflow;
        }
        for (std::size_t i = 0; i < text.size(); ++i) {
            storage_.pattern[length + i] = {text[i], quoted};
        }
        length += text.size();
        return Status::Ok;
    };
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        const auto& fragment = fragments[i];
        switch (fragment.kind) {
            case FragmentKind::Literal:
            case FragmentKind::SingleQuoted:
            case FragmentKind::DoubleQuoted:
            case FragmentKind::Escaped: {
                const auto quoted = fragment.kind != FragmentKind::Literal;
                const auto status = append(
                    script.source.slice(fragment.source), quoted);
                if (status != Status::Ok) return status;
                break;
            }
            case FragmentKind::Parameter:
            case FragmentKind::Arithmetic: {
                FieldView expanded;
                const auto outcome = expand_value(
                    script.source, fragments.subspan(i, 1), 0,
                    context_->state, storage_.expansion, expanded);
                if (outcome.status != Status::Ok) return outcome.status;
                if (expanded.fields.size() > 1) return Status::Unsupported;
                // Expansion results are compared literally at level 1; a
                // generated metacharacter never becomes a wildcard.
                const auto status = append(single_field(expanded), true);
                if (status != Status::Ok) return status;
                break;
            }
            default:
                return Status::Unsupported;
        }
    }
    return Status::Ok;
}

CommandResult Evaluator::assign_prefixes(std::size_t node_id,
                                         std::size_t count) {
    const auto& script = *script_;
    const auto& node = script.nodes[node_id];
    for (std::size_t index = 0; index < count; ++index) {
        const auto child = child_at(script, node, index);
        if (child == no_node) return failure(2, Status::BadArgument);
        const auto spelling = word_text(script, child);
        const auto equal = spelling.find('=');
        const auto fragments = word_fragments(script, child);
        if (equal == std::string_view::npos || fragments.empty()) {
            return failure(2, Status::BadArgument);
        }
        const auto& token = script.tokens[script.nodes[child].token_index];
        FieldView expanded;
        const auto outcome = expand_value(
            script.source, fragments,
            token.source.offset + equal + 1, context_->state,
            storage_.expansion, expanded);
        if (outcome.status != Status::Ok) {
            auto result = failure(1, outcome.status);
            result.overflow = outcome.overflow;
            return result;
        }
        if (expanded.fields.size() > 1) {
            return failure(2, Status::Unsupported);
        }
        const auto assigned = context_->state.assign(
            spelling.substr(0, equal), single_field(expanded));
        if (!assigned.ok()) {
            auto result = failure(2, assigned.status);
            result.overflow = assigned.overflow;
            return result;
        }
    }
    return {};
}

Evaluator::Outcome Evaluator::run_simple(std::size_t node_id, bool negate) {
    const auto& script = *script_;
    const auto& node = script.nodes[node_id];
    const auto fail = [this](int status, Status error) {
        active_ = false;
        return Outcome{.returns = true,
                       .result = {.step = Step::Failed,
                                  .command = failure(status, error)}};
    };

    std::size_t assignments = 0;
    while (assignments < node.link_count) {
        const auto child = child_at(script, node, assignments);
        if (child == no_node) return fail(2, Status::BadArgument);
        if (script.nodes[child].kind != SyntaxKind::Assignment) break;
        ++assignments;
    }

    std::size_t argument_count = 0;
    std::size_t text_used = 0;
    bool failed = false;
    CommandResult result{};
    for (std::size_t index = assignments;
         !failed && index < node.link_count; ++index) {
        const auto child = child_at(script, node, index);
        if (child == no_node) return fail(2, Status::BadArgument);
        if (script.nodes[child].kind != SyntaxKind::Word) {
            failed = true;
            result = failure(2, Status::Unsupported);
            break;
        }
        if (script.nodes[child].token_index >= script.tokens.size()) {
            return fail(2, Status::BadArgument);
        }
        const auto fragments = word_fragments(script, child);
        FieldView expanded;
        const auto outcome = expand_word(
            script.source, fragments, context_->state,
            storage_.expansion, expanded);
        if (outcome.status != Status::Ok) {
            failed = true;
            result = failure(1, outcome.status);
            result.overflow = outcome.overflow;
            break;
        }
        for (std::size_t field = 0;
             field < expanded.fields.size(); ++field) {
            if (argument_count == storage_.arguments.size()) {
                failed = true;
                result = failure(2, Status::Overflow);
                result.overflow = {StorageClass::ExpandedFields,
                                   argument_count + 1};
                break;
            }
            const auto value = expanded.field(field);
            if (value.size() > storage_.argument_text.size() - text_used) {
                failed = true;
                result = failure(2, Status::Overflow);
                result.overflow = {StorageClass::ExpandedFieldText,
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
                storage_.argument_text.data() + text_used, value.size()};
            text_used += value.size();
        }
    }

    if (!failed && argument_count == 0) {
        result = assign_prefixes(node_id, assignments);
    } else if (!failed) {
        const auto* descriptor = registry_->find(storage_.arguments[0]);
        const std::span<const std::string_view> args{
            storage_.arguments.data(), argument_count};
        // Handlers see the staging sinks, never the application's transport.
        auto staged = handler_io();
        CommandContext handler{staged, context_->state,
                               context_->capabilities, context_->scratch};
        if (descriptor == nullptr) {
            result = failure(static_cast<int>(CommandStatus::NotFound),
                             Status::NotFound);
        } else if (assignments == 0) {
            result = dispatch(*descriptor, args, handler);
        } else {
            // Prefix assignments are visible to the handler. Only a special
            // builtin keeps them after the invocation returns.
            const auto persist = descriptor->command_class ==
                                 CommandClass::SpecialBuiltin;
            ShellState restore;
            auto saved = true;
            if (!persist) {
                const auto fork = context_->state.fork_variables(
                    storage_.prefix_variables,
                    storage_.prefix_variable_text, restore);
                if (!fork.ok()) {
                    saved = false;
                    result = failure(2, fork.status);
                    result.overflow = fork.overflow;
                }
            }
            if (saved) {
                result = assign_prefixes(node_id, assignments);
                if (result.error == Status::Ok) {
                    result = dispatch(*descriptor, args, handler);
                }
                if (!persist) {
                    const auto reverted =
                        context_->state.commit_variables_from(restore);
                    if (!reverted.ok() && result.error == Status::Ok) {
                        result = failure(2, reverted.status);
                        result.overflow = reverted.overflow;
                    }
                }
            }
        }
    }

    if (negate) result.status = result.status == 0 ? 1 : 0;
    last_status_ = result.status;
    context_->state.last_status = last_status_;

    switch (result.flow) {
        case Flow::Normal:
            return deliver({.step = Step::Running, .command = result});
        case Flow::Yield:
            return deliver({.step = Step::Yielded, .command = result});
        case Flow::Exit:
            active_ = false;
            return deliver({.step = Step::Complete, .command = result});
        case Flow::Break:
        case Flow::Continue:
            if (!unwind_to_loop(result.flow == Flow::Break)) {
                result = failure(2, Status::BadArgument);
                last_status_ = result.status;
                context_->state.last_status = last_status_;
            }
            return deliver({.step = Step::Running, .command = result});
        case Flow::Return:
        case Flow::Replace:
            // Function and installed-script frames arrive with change set 8.
            result = failure(2, Status::BadArgument);
            last_status_ = result.status;
            context_->state.last_status = last_status_;
            return deliver({.step = Step::Running, .command = result});
    }
    return deliver({.step = Step::Running, .command = result});
}

Evaluator::Outcome Evaluator::enter_command(std::size_t node_id,
                                            bool tested) {
    const auto& script = *script_;
    const auto fail = [this](int status, Status error) {
        active_ = false;
        return Outcome{.returns = true,
                       .result = {.step = Step::Failed,
                                  .command = failure(status, error)}};
    };
    auto negate = false;
    while (node_id < script.nodes.size() &&
           script.nodes[node_id].kind == SyntaxKind::Negation) {
        const auto inner = child_at(script, script.nodes[node_id], 0);
        if (inner == no_node) return fail(2, Status::BadArgument);
        negate = !negate;
        node_id = inner;
    }
    if (node_id >= script.nodes.size()) {
        return fail(2, Status::BadArgument);
    }
    auto kind = FrameKind::Brace;
    switch (script.nodes[node_id].kind) {
        case SyntaxKind::Simple:
            tested_ = tested || negate;
            return run_simple(node_id, negate);
        case SyntaxKind::If: kind = FrameKind::If; break;
        case SyntaxKind::While: kind = FrameKind::While; break;
        case SyntaxKind::For: kind = FrameKind::For; break;
        case SyntaxKind::Case: kind = FrameKind::Case; break;
        case SyntaxKind::BraceGroup: kind = FrameKind::Brace; break;
        default:
            // Function definitions arrive with change set 8.
            return fail(2, Status::Unsupported);
    }
    const auto pushed = push(kind, node_id, tested, negate);
    if (pushed != Status::Ok) {
        active_ = false;
        auto result = failure(2, pushed);
        result.overflow = {StorageClass::EvaluatorFrames, frame_count_ + 1};
        return {.returns = true,
                .result = {.step = Step::Failed, .command = result}};
    }
    return {};
}

Status Evaluator::begin(const EmbeddedScript& script, Registry& registry,
                        CommandContext& context,
                        EvaluatorStorage storage) {
    active_ = false;
    frame_count_ = 0;
    items_used_ = 0;
    item_text_used_ = 0;
    last_status_ = 0;
    tested_ = false;
    held_ = {};
    holding_ = false;
    if (script.root >= script.nodes.size() ||
        script.nodes[script.root].kind != SyntaxKind::Program ||
        script.nodes[script.root].link_count != 1) {
        return Status::BadArgument;
    }
    const auto program = list_child(script, script.nodes[script.root], 0);
    if (program == no_node) return Status::BadArgument;
    script_ = &script;
    registry_ = &registry;
    context_ = &context;
    storage_ = storage;
    staging_out_ = MemorySink{storage.staged_output,
                              StorageClass::StagedOutput};
    staging_error_ = MemorySink{storage.staged_error,
                                StorageClass::StagedOutput};
    drained_out_ = 0;
    drained_error_ = 0;
    drain_failure_ = {};
    const auto pushed = push(FrameKind::List, program, false, false);
    if (pushed != Status::Ok) return pushed;
    active_ = true;
    return Status::Ok;
}

StepResult Evaluator::step(std::size_t operation_budget) {
    if (script_ == nullptr) {
        return {.step = Step::Complete, .command = {.status = last_status_}};
    }
    // Staged bytes reach the application before any further work, so no
    // command is selected while output from the previous one is pending.
    if (pending_output()) {
        if (operation_budget == 0) return {.step = Step::Running};
        const auto outcome = drain_once();
        if (outcome == Drain::Failed) {
            active_ = false;
            holding_ = false;
            auto result = failure(static_cast<int>(CommandStatus::Failure),
                                  drain_failure_.error == Status::Ok
                                      ? Status::WriteError
                                      : drain_failure_.error);
            result.overflow = drain_failure_.overflow;
            return {.step = Step::Failed, .command = result};
        }
        if (pending_output()) return {.step = Step::Running};
        recycle_staging();
    }
    if (holding_) {
        holding_ = false;
        const auto result = held_;
        held_ = {};
        return result;
    }
    if (!active_) {
        return {.step = Step::Complete, .command = {.status = last_status_}};
    }
    const auto& script = *script_;
    const auto fail = [this](int status, Status error) {
        active_ = false;
        return StepResult{.step = Step::Failed,
                          .command = failure(status, error)};
    };
    const auto exhausted = [this](StorageClass storage_class,
                                  std::size_t required) {
        active_ = false;
        auto result = failure(2, Status::Overflow);
        result.overflow = {storage_class, required};
        return StepResult{.step = Step::Failed, .command = result};
    };

    for (std::size_t used = 0; used < operation_budget; ++used) {
        if (frame_count_ == 0) {
            active_ = false;
            return {.step = Step::Complete,
                    .command = {.status = last_status_}};
        }
        auto& frame = storage_.frames[frame_count_ - 1];
        if (frame.node >= script.nodes.size()) {
            return fail(2, Status::BadArgument);
        }
        const auto& node = script.nodes[frame.node];
        switch (frame.kind) {
            case FrameKind::List: {
                if (frame.link >= node.link_count) {
                    complete_frame(last_status_);
                    break;
                }
                const auto child = child_at(script, node, frame.link);
                if (child == no_node ||
                    script.nodes[child].kind != SyntaxKind::AndOr) {
                    return fail(2, Status::BadArgument);
                }
                ++frame.link;
                if (push(FrameKind::AndOr, child, frame.tested, false) !=
                    Status::Ok) {
                    return exhausted(StorageClass::EvaluatorFrames,
                                     frame_count_ + 1);
                }
                break;
            }
            case FrameKind::AndOr: {
                if (frame.link >= node.link_count) {
                    complete_frame(last_status_);
                    break;
                }
                const auto* entry = link_at(script, node, frame.link);
                if (entry == nullptr ||
                    entry->child >= script.nodes.size()) {
                    return fail(2, Status::BadArgument);
                }
                if (frame.link != 0 &&
                    ((entry->join == SyntaxJoin::And && last_status_ != 0) ||
                     (entry->join == SyntaxJoin::Or &&
                      last_status_ == 0))) {
                    ++frame.link;
                    break;
                }
                const auto child = entry->child;
                const auto tested =
                    frame.tested || frame.link + 1 < node.link_count;
                ++frame.link;
                const auto outcome = enter_command(child, tested);
                if (outcome.returns) return outcome.result;
                break;
            }
            case FrameKind::If: {
                if (node.link_count < 2) {
                    return fail(2, Status::BadArgument);
                }
                if (frame.phase == 0) {
                    const auto condition = list_child(script, node, 0);
                    if (condition == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 1;
                    if (push(FrameKind::List, condition, true, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                if (frame.phase == 1) {
                    if (last_status_ != 0) {
                        frame.link = 2;
                        frame.phase = 2;
                        break;
                    }
                    const auto body = list_child(script, node, 1);
                    if (body == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 3;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                if (frame.phase == 2) {
                    if (frame.link >= node.link_count) {
                        complete_frame(0);
                        break;
                    }
                    const auto branch = child_at(script, node, frame.link);
                    if (branch == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    ++frame.link;
                    const auto& alternative = script.nodes[branch];
                    if (alternative.kind == SyntaxKind::Elif) {
                        const auto test = list_child(script, alternative, 0);
                        if (alternative.link_count != 2 ||
                            test == no_node) {
                            return fail(2, Status::BadArgument);
                        }
                        frame.aux = branch;
                        frame.phase = 4;
                        if (push(FrameKind::List, test, true, false) !=
                            Status::Ok) {
                            return exhausted(StorageClass::EvaluatorFrames,
                                             frame_count_ + 1);
                        }
                        break;
                    }
                    if (alternative.kind != SyntaxKind::Else) {
                        return fail(2, Status::BadArgument);
                    }
                    const auto body = list_child(script, alternative, 0);
                    if (alternative.link_count != 1 || body == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 3;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                if (frame.phase == 4) {
                    if (last_status_ != 0) {
                        frame.phase = 2;
                        break;
                    }
                    const auto& alternative = script.nodes[frame.aux];
                    const auto body = list_child(script, alternative, 1);
                    if (body == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 3;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                complete_frame(last_status_);
                break;
            }
            case FrameKind::While: {
                if (node.link_count != 2) {
                    return fail(2, Status::BadArgument);
                }
                if (frame.phase == 0) {
                    const auto test = list_child(script, node, 0);
                    if (test == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 1;
                    if (push(FrameKind::List, test, true, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                if (frame.phase == 1) {
                    if (last_status_ != 0) {
                        complete_frame(frame.status);
                        break;
                    }
                    const auto body = list_child(script, node, 1);
                    if (body == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 2;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                frame.status = last_status_;
                frame.phase = 0;
                break;
            }
            case FrameKind::For: {
                if (node.link_count < 2) {
                    return fail(2, Status::BadArgument);
                }
                if (frame.phase == 0) {
                    if (frame.cursor == 0) {
                        frame.item_first = items_used_;
                        frame.link = 1;
                        frame.cursor = 1;
                    }
                    if (node.has_in_clause) {
                        if (frame.link + 1 < node.link_count) {
                            const auto value = child_at(script, node,
                                                        frame.link);
                            if (value == no_node ||
                                script.nodes[value].kind !=
                                    SyntaxKind::Word) {
                                return fail(2, Status::BadArgument);
                            }
                            ++frame.link;
                            FieldView expanded;
                            const auto outcome = expand_word(
                                script.source,
                                word_fragments(script, value),
                                context_->state, storage_.expansion,
                                expanded);
                            if (outcome.status != Status::Ok) {
                                return fail(1, outcome.status);
                            }
                            used += expanded.text.size() / bytes_per_unit;
                            for (std::size_t i = 0;
                                 i < expanded.fields.size(); ++i) {
                                if (append_item(expanded.field(i)) !=
                                    Status::Ok) {
                                    return exhausted(
                                        StorageClass::ExpandedFields,
                                        items_used_ + 1);
                                }
                            }
                            break;
                        }
                    } else {
                        if (frame.cursor <=
                            context_->state.argument_count()) {
                            const auto value =
                                context_->state.positional(frame.cursor);
                            ++frame.cursor;
                            if (!value.found) {
                                return fail(2, Status::BadArgument);
                            }
                            used += value.value.size() / bytes_per_unit;
                            if (append_item(value.value) != Status::Ok) {
                                return exhausted(
                                    StorageClass::ExpandedFields,
                                    items_used_ + 1);
                            }
                            break;
                        }
                    }
                    frame.item_count = items_used_ - frame.item_first;
                    frame.cursor = 0;
                    frame.status = 0;
                    frame.phase = 1;
                    break;
                }
                if (frame.phase == 1) {
                    if (frame.cursor >= frame.item_count) {
                        complete_frame(frame.status);
                        break;
                    }
                    const auto name = word_text(
                        script, child_at(script, node, 0));
                    const auto value =
                        storage_.loop_items[frame.item_first +
                                            frame.cursor];
                    ++frame.cursor;
                    const auto assigned =
                        context_->state.assign(name, value);
                    if (!assigned.ok()) {
                        return exhausted(assigned.overflow.storage_class,
                                         assigned.overflow.required);
                    }
                    const auto body = last_child(script, node);
                    if (body == no_node ||
                        script.nodes[body].kind != SyntaxKind::List) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 2;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                frame.status = last_status_;
                frame.phase = 1;
                break;
            }
            case FrameKind::Case: {
                if (node.link_count == 0) {
                    return fail(2, Status::BadArgument);
                }
                if (frame.phase == 0) {
                    const auto selector = child_at(script, node, 0);
                    if (selector == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    FieldView expanded;
                    const auto outcome = expand_value(
                        script.source, word_fragments(script, selector), 0,
                        context_->state, storage_.expansion, expanded);
                    if (outcome.status != Status::Ok) {
                        return fail(1, outcome.status);
                    }
                    if (expanded.fields.size() > 1) {
                        return fail(2, Status::Unsupported);
                    }
                    const auto value = single_field(expanded);
                    used += value.size() / bytes_per_unit;
                    frame.item_first = items_used_;
                    if (append_item(value) != Status::Ok) {
                        return exhausted(StorageClass::ExpandedFieldText,
                                         item_text_used_ + value.size());
                    }
                    frame.item_count = 1;
                    frame.link = 1;
                    frame.phase = 1;
                    break;
                }
                if (frame.phase == 1) {
                    if (frame.link >= node.link_count) {
                        complete_frame(0);
                        break;
                    }
                    const auto item = child_at(script, node, frame.link);
                    if (item == no_node ||
                        script.nodes[item].kind != SyntaxKind::CaseItem ||
                        script.nodes[item].link_count < 2) {
                        return fail(2, Status::BadArgument);
                    }
                    ++frame.link;
                    frame.aux = item;
                    frame.cursor = 0;
                    frame.phase = 2;
                    break;
                }
                if (frame.phase == 2) {
                    const auto& item = script.nodes[frame.aux];
                    if (frame.cursor + 1 >= item.link_count) {
                        frame.phase = 1;
                        break;
                    }
                    const auto pattern = child_at(script, item,
                                                  frame.cursor);
                    ++frame.cursor;
                    if (pattern == no_node ||
                        script.nodes[pattern].kind !=
                            SyntaxKind::Pattern) {
                        return fail(2, Status::BadArgument);
                    }
                    std::size_t length = 0;
                    const auto built = build_pattern(pattern, length);
                    if (built != Status::Ok) return fail(2, built);
                    used += length / bytes_per_unit;
                    const auto matched = match_case_pattern(
                        storage_.pattern.first(length),
                        storage_.loop_items[frame.item_first]);
                    if (matched.status != Status::Ok) {
                        return fail(2, matched.status);
                    }
                    if (!matched.matched) break;
                    const auto body = last_child(script, item);
                    if (body == no_node ||
                        script.nodes[body].kind != SyntaxKind::List) {
                        return fail(2, Status::BadArgument);
                    }
                    // An empty case body succeeds rather than inheriting
                    // the status of the command before the case.
                    last_status_ = 0;
                    context_->state.last_status = 0;
                    frame.phase = 3;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                complete_frame(last_status_);
                break;
            }
            case FrameKind::Brace: {
                if (node.link_count != 1) {
                    return fail(2, Status::BadArgument);
                }
                if (frame.phase == 0) {
                    const auto body = list_child(script, node, 0);
                    if (body == no_node) {
                        return fail(2, Status::BadArgument);
                    }
                    frame.phase = 1;
                    if (push(FrameKind::List, body, frame.tested, false) !=
                        Status::Ok) {
                        return exhausted(StorageClass::EvaluatorFrames,
                                         frame_count_ + 1);
                    }
                    break;
                }
                complete_frame(last_status_);
                break;
            }
        }
    }
    return {.step = Step::Running};
}

}  // namespace mm::shell
