// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :execute;
import :function;
import :interpret;
import :parse;
import :service;
import :state;
import :syntax;
import :trap;
import :word;
import mm.shell;

namespace mm::shell::full {
namespace {

[[nodiscard]] bool assignment_word(std::string_view word) {
    const auto equal = word.find('=');
    if (equal == std::string_view::npos || equal == 0) return false;
    const auto name = word.substr(0, equal);
    const auto first = name.front();
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
          first == '_')) {
        return false;
    }
    for (const char c : name.substr(1)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool keeps_assignments(std::string_view name) {
    // POSIX: a special builtin and a function keep a prefix assignment; a
    // regular builtin and an external do not.
    return name == ":" || name == "break" || name == "continue" ||
           name == "exit" || name == "return" || name == "set" ||
           name == "shift" || name == "export" || name == "trap" ||
           name == "exec";
}

[[nodiscard]] bool parse_count(std::string_view text, std::size_t& out) {
    if (text.empty()) return false;
    out = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        out = out * 10 + static_cast<std::size_t>(c - '0');
    }
    return true;
}

[[nodiscard]] int evaluate_test(std::span<const std::string> operands,
                                bool& usage) {
    usage = false;
    if (operands.empty()) return 1;
    if (operands[0] == "!") {
        const auto inner = evaluate_test(operands.subspan(1), usage);
        return usage ? 2 : (inner == 0 ? 1 : 0);
    }
    if (operands.size() == 1) return operands[0].empty() ? 1 : 0;
    if (operands.size() == 2) {
        if (operands[0] == "-n") return operands[1].empty() ? 1 : 0;
        if (operands[0] == "-z") return operands[1].empty() ? 0 : 1;
        usage = true;
        return 2;
    }
    if (operands.size() == 3) {
        const auto& op = operands[1];
        if (op == "=") return operands[0] == operands[2] ? 0 : 1;
        if (op == "!=") return operands[0] != operands[2] ? 0 : 1;
        auto numeric = NumericOperator::Eq;
        if (op == "-eq") numeric = NumericOperator::Eq;
        else if (op == "-ne") numeric = NumericOperator::Ne;
        else if (op == "-lt") numeric = NumericOperator::Lt;
        else if (op == "-le") numeric = NumericOperator::Le;
        else if (op == "-gt") numeric = NumericOperator::Gt;
        else if (op == "-ge") numeric = NumericOperator::Ge;
        else {
            usage = true;
            return 2;
        }
        const auto compared = numeric_test(numeric, operands[0],
                                           operands[2]);
        if (compared.status != ArithmeticStatus::Ok) {
            usage = true;
            return 2;
        }
        return compared.value ? 0 : 1;
    }
    usage = true;
    return 2;
}

[[nodiscard]] std::string format_printf(
    std::string_view format, std::span<const std::string> operands,
    bool& unsupported) {
    unsupported = false;
    std::string out;
    std::size_t next = 0;
    for (;;) {
        const auto consumed = next;
        for (std::size_t i = 0; i < format.size(); ++i) {
            if (format[i] == '\\' && i + 1 < format.size()) {
                ++i;
                switch (format[i]) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case '\\': out.push_back('\\'); break;
                    default:
                        out.push_back('\\');
                        out.push_back(format[i]);
                        break;
                }
                continue;
            }
            if (format[i] == '%' && i + 1 < format.size()) {
                ++i;
                if (format[i] == '%') {
                    out.push_back('%');
                    continue;
                }
                if (format[i] != 's') {
                    unsupported = true;
                    return out;
                }
                if (next < operands.size()) out.append(operands[next++]);
                continue;
            }
            out.push_back(format[i]);
        }
        if (next >= operands.size() || next == consumed) return out;
    }
}

}  // namespace

Interpreter::Step Interpreter::run_simple(const FullScript& script,
                                          std::size_t node, bool tested) {
    (void)tested;
    const auto& simple = script.nodes[node];
    Step failure;
    Command command;
    command.streams = streams_;

    // Words live in the node's token range; only redirections are children, so
    // a token inside a redirection's range is not a command word.
    std::vector<std::size_t> words;
    for (auto token = simple.first_token;
         token <= simple.last_token && token < script.tokens.size();
         ++token) {
        if (script.tokens[token].kind != TokenKind::Word) continue;
        auto redirected = false;
        for (const auto child : simple.children) {
            const auto& part = script.nodes[child.node];
            if (part.kind != NodeKind::Redirection) continue;
            if (token >= part.first_token && token <= part.last_token) {
                redirected = true;
                break;
            }
        }
        if (!redirected) words.push_back(token);
    }

    std::vector<std::string> names;
    std::vector<std::string> values;
    std::size_t index = 0;
    for (; index < words.size(); ++index) {
        const auto spelling = script.text(script.tokens[words[index]].source);
        if (!assignment_word(spelling)) break;
        const auto equal = spelling.find('=');
        const Substituter substituter{this, &Interpreter::substitute};
        const auto expanded = expand_word_full(
            spelling.substr(equal + 1), state_.core(),
            {false, false, state_.directory()}, services_.file, substituter);
        if (!expanded.ok()) {
            failure.status = 1;
            failure.service = expanded.service;
            failure.diagnostic = {ParseStatus::Malformed, 0,
                                  "assignment value"};
            return failure;
        }
        names.emplace_back(spelling.substr(0, equal));
        values.push_back(expanded.fields.empty() ? std::string{}
                                                : expanded.fields[0]);
    }

    last_substitution_ = 0;
    for (; index < words.size(); ++index) {
        if (!expand_fields(script, words[index], true, true,
                           command.arguments, failure)) {
            return failure;
        }
    }

    if (!apply_redirections(script, node, command, failure)) {
        release(command);
        return failure;
    }

    if (command.arguments.empty()) {
        // An assignment-only command persists, and its status is that of the
        // last substitution its values ran.
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (state_.core().assign(names[i], values[i]).ok()) continue;
            release(command);
            failure.status = 1;
            failure.diagnostic = {ParseStatus::Malformed, 0,
                                  "variable capacity"};
            return failure;
        }
        release(command);
        Step step;
        step.status = last_substitution_;
        state_.core().last_status = step.status;
        if (state_.core().errexit && step.status != 0 &&
            tested_ == 0) {
            step.flow = Flow::Exit;
        }
        return step;
    }

    const auto& name = command.arguments[0];
    const auto* function = functions_.find(name);
    const auto persist = keeps_assignments(name) || function != nullptr;

    // A non-persisting prefix is restored afterwards, so IFS= read leaves IFS
    // as it was.
    std::vector<std::string> saved;
    std::vector<bool> had;
    if (!persist) {
        for (const auto& entry : names) {
            const auto previous = state_.core().lookup(entry);
            had.push_back(previous.found);
            saved.emplace_back(previous.value);
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (state_.core().assign(names[i], values[i]).ok()) continue;
        release(command);
        failure.status = 1;
        failure.diagnostic = {ParseStatus::Malformed, 0,
                              "variable capacity"};
        return failure;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        command.assignments.push_back(names[i] + "=" + values[i]);
    }

    Step step;
    auto handled = false;
    if (function != nullptr) {
        const auto body = functions_.body(name);
        step = body == nullptr
                   ? Step{.status = 1}
                   : call(*body, command);
    } else {
        step = builtin(command, handled);
        if (!handled) step = external(command);
    }

    if (!persist) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            // Restoring an unset name to empty is the closest this level gets
            // to unsetting it, and it is what the corpus's IFS= needs.
            (void)state_.core().assign(names[i], had[i] ? saved[i]
                                                        : std::string{});
        }
    }
    release(command);
    state_.core().last_status = step.status;
    if (step.flow == Flow::Normal && state_.core().errexit &&
        step.status != 0 && tested_ == 0) {
        step.flow = Flow::Exit;
    }
    return step;
}

// Every stage after the first must be an external program: a builtin or a
// function there would need its own process, which no abstract service offers.
Interpreter::Step Interpreter::run_pipeline_node(const FullScript& script,
                                                 std::size_t node,
                                                 bool tested) {
    (void)tested;
    const auto& children = script.nodes[node].children;
    if (children.empty()) {
        return {.status = 2,
                .diagnostic = {ParseStatus::Malformed, 0, "empty pipeline"}};
    }
    if (children.size() == 1) {
        return run_node(script, children[0].node);
    }
    if (services_.io.pipe == nullptr) {
        return {.status = 1, .service = ServiceStatus::Invalid};
    }

    // The first stage runs in this shell with its output on a pipe, so a
    // builtin such as echo or printf can feed the rest.
    Handle read_end = invalid_handle;
    Handle write_end = invalid_handle;
    if (services_.io.pipe(services_.io.context, read_end, write_end) !=
        ServiceStatus::Ok) {
        return {.status = 1, .service = ServiceStatus::Failed};
    }
    const auto saved = streams_;
    streams_.output = write_end;
    auto first = run_node(script, children[0].node);
    streams_ = saved;
    (void)services_.io.close(services_.io.context, write_end);
    if (!first.ok()) {
        (void)services_.io.close(services_.io.context, read_end);
        return first;
    }

    // The remaining stages are one process each, wired by the service.
    std::vector<std::vector<std::string>> arguments;
    std::vector<std::string> environment;
    state_.environment(environment);
    Step failure;
    for (std::size_t i = 1; i < children.size(); ++i) {
        const auto& stage = script.nodes[children[i].node];
        if (stage.kind != NodeKind::Simple) {
            (void)services_.io.close(services_.io.context, read_end);
            return {.status = 2,
                    .diagnostic = {ParseStatus::Unsupported, 0,
                                   "only a simple command may follow a pipe"}};
        }
        std::vector<std::string> fields;
        for (auto token = stage.first_token;
             token <= stage.last_token && token < script.tokens.size();
             ++token) {
            if (script.tokens[token].kind != TokenKind::Word) continue;
            if (!expand_fields(script, token, true, true, fields, failure)) {
                (void)services_.io.close(services_.io.context, read_end);
                return failure;
            }
        }
        if (fields.empty()) {
            (void)services_.io.close(services_.io.context, read_end);
            return {.status = 2,
                    .diagnostic = {ParseStatus::Malformed, 0,
                                   "pipeline stage has no command"}};
        }
        arguments.push_back(fields);
    }

    std::vector<std::vector<std::string_view>> views;
    std::vector<std::string_view> environment_views;
    for (const auto& entry : environment) environment_views.push_back(entry);
    views.reserve(arguments.size());
    for (const auto& stage : arguments) {
        std::vector<std::string_view> stage_views;
        stage_views.reserve(stage.size());
        for (const auto& field : stage) stage_views.push_back(field);
        views.push_back(stage_views);
    }
    std::vector<ProcessRequest> stages;
    stages.reserve(views.size());
    for (std::size_t i = 0; i < views.size(); ++i) {
        ProcessRequest request;
        request.arguments = views[i];
        request.environment = environment_views;
        request.directory = state_.directory();
        if (i == 0) request.input = read_end;
        if (i + 1 == views.size()) {
            request.output = streams_.output;
        }
        request.error = streams_.error;
        stages.push_back(request);
        ExternalRecord record;
        record.arguments = arguments[i];
        record.environment = environment;
        record.directory = std::string{state_.directory()};
        externals_.push_back(record);
        ++spawns_;
    }
    const auto outcome = run_pipeline(stages, services_);
    (void)services_.io.close(services_.io.context, read_end);

    Step step;
    step.status = outcome.ok() ? outcome.exit_status : 127;
    step.service = outcome.ok() ? ServiceStatus::Ok : ServiceStatus::Ok;
    state_.core().last_status = step.status;
    if (state_.core().errexit && step.status != 0 && tested_ == 0) {
        step.flow = Flow::Exit;
    }
    return step;
}

Interpreter::Step Interpreter::external(const Command& command) {
    Step step;
    if (services_.process.spawn == nullptr) {
        step.status = 127;
        return step;
    }
    std::vector<std::string> environment;
    state_.environment(environment);
    for (const auto& entry : command.assignments) {
        environment.push_back(entry);
    }
    std::vector<std::string_view> arguments;
    arguments.reserve(command.arguments.size());
    for (const auto& field : command.arguments) arguments.push_back(field);
    std::vector<std::string_view> environment_views;
    environment_views.reserve(environment.size());
    for (const auto& entry : environment) environment_views.push_back(entry);

    ProcessRequest request;
    request.arguments = arguments;
    request.environment = environment_views;
    request.directory = state_.directory();
    request.input = command.streams.input;
    request.output = command.streams.output;
    request.error = command.streams.error;

    ExternalRecord record;
    record.arguments = command.arguments;
    record.environment = environment;
    record.directory = std::string{state_.directory()};
    externals_.push_back(record);

    Handle child = invalid_handle;
    const auto spawned = services_.process.spawn(
        services_.process.context, request, child);
    if (spawned == ServiceStatus::NotFound) {
        (void)emit(command.streams.error,
                   command.arguments[0] + ": not found\n");
        step.status = 127;
        return step;
    }
    if (spawned == ServiceStatus::PermissionDenied) {
        (void)emit(command.streams.error,
                   command.arguments[0] + ": cannot execute\n");
        step.status = 126;
        return step;
    }
    if (spawned != ServiceStatus::Ok) {
        step.status = 1;
        step.service = spawned;
        return step;
    }
    ++spawns_;
    int status = 0;
    const auto waited = services_.process.wait(services_.process.context,
                                               child, status);
    if (waited != ServiceStatus::Ok) {
        step.status = 1;
        step.service = waited;
        return step;
    }
    step.status = status;
    return step;
}

// A function body runs in this shell with its own positional parameters, and
// return leaves only the body.
Interpreter::Step Interpreter::call(const FullScript& body,
                                    const Command& command) {
    Step step;
    if (depth_ >= 64) {
        step.status = 2;
        step.diagnostic = {ParseStatus::Malformed, 0, "call depth"};
        return step;
    }
    std::vector<std::string_view> arguments;
    for (std::size_t i = 1; i < command.arguments.size(); ++i) {
        arguments.push_back(command.arguments[i]);
    }
    std::vector<PositionalSlot> saved(64);
    PositionalFrame frame;
    if (!state_.core()
             .push_positionals(command.arguments[0], arguments, saved, frame)
             .ok()) {
        step.status = 1;
        step.diagnostic = {ParseStatus::Malformed, 0,
                           "positional capacity"};
        return step;
    }
    const auto saved_streams = streams_;
    const auto was_in_function = in_function_;
    streams_ = command.streams;
    in_function_ = true;
    ++depth_;
    const auto& program = body.nodes[body.root];
    for (const auto& child : program.children) {
        step = run_node(body, child.node);
        if (step.flow == Flow::Return) {
            step.flow = Flow::Normal;
            break;
        }
        if (step.flow != Flow::Normal || !step.ok()) break;
    }
    --depth_;
    in_function_ = was_in_function;
    streams_ = saved_streams;
    (void)state_.core().pop_positionals(saved, frame);
    return step;
}

Interpreter::Step Interpreter::builtin(const Command& command,
                                       bool& handled) {
    handled = true;
    Step step;
    const auto& name = command.arguments[0];
    const std::span<const std::string> operands{
        command.arguments.data() + 1, command.arguments.size() - 1};

    if (name == ":" || name == "true") return step;
    if (name == "false") {
        step.status = 1;
        return step;
    }
    if (name == "echo") {
        std::string text;
        for (std::size_t i = 0; i < operands.size(); ++i) {
            if (i != 0) text.push_back(' ');
            text.append(operands[i]);
        }
        text.push_back('\n');
        if (!emit(command.streams.output, text)) step.status = 1;
        return step;
    }
    if (name == "printf") {
        if (operands.empty()) {
            step.status = 2;
            return step;
        }
        auto unsupported = false;
        const auto text = format_printf(operands[0],
                                        operands.subspan(1), unsupported);
        if (!emit(command.streams.output, text)) step.status = 1;
        if (unsupported) step.status = 1;
        return step;
    }
    if (name == "test" || name == "[") {
        auto arguments = operands;
        if (name == "[") {
            if (arguments.empty() || arguments.back() != "]") {
                step.status = 2;
                return step;
            }
            arguments = arguments.first(arguments.size() - 1);
        }
        auto usage = false;
        step.status = evaluate_test(arguments, usage);
        return step;
    }
    if (name == "pwd") {
        if (!emit(command.streams.output,
                  std::string{state_.directory()} + "\n")) {
            step.status = 1;
        }
        return step;
    }
    if (name == "cd") {
        if (operands.size() > 1) {
            step.status = 2;
            return step;
        }
        const auto target = operands.empty()
                                ? std::string{state_.core().lookup("HOME")
                                                  .value}
                                : operands[0];
        if (target.empty()) {
            step.status = 1;
            return step;
        }
        std::string resolved = target;
        if (!target.empty() && target.front() != '/') {
            resolved = std::string{state_.directory()};
            if (!resolved.empty() && resolved.back() != '/') {
                resolved.push_back('/');
            }
            resolved.append(target);
        }
        auto exists = false;
        auto directory = false;
        if (services_.file.status == nullptr ||
            services_.file.status(services_.file.context, resolved, exists,
                                  directory) != ServiceStatus::Ok ||
            !exists || !directory) {
            (void)emit(command.streams.error,
                       "cd: " + target + ": not a directory\n");
            step.status = 1;
            return step;
        }
        state_.set_directory(resolved);
        (void)state_.core().assign("PWD", resolved);
        return step;
    }
    if (name == "exit") {
        step.flow = Flow::Exit;
        step.status = state_.core().last_status;
        if (!operands.empty()) {
            std::size_t value = 0;
            if (!parse_count(operands[0], value) || value > 255) {
                step.flow = Flow::Normal;
                step.status = 2;
                return step;
            }
            step.status = static_cast<int>(value);
        }
        return step;
    }
    if (name == "return") {
        if (!in_function_) {
            step.status = 2;
            return step;
        }
        step.flow = Flow::Return;
        step.status = state_.core().last_status;
        if (!operands.empty()) {
            std::size_t value = 0;
            if (!parse_count(operands[0], value) || value > 255) {
                step.flow = Flow::Normal;
                step.status = 2;
                return step;
            }
            step.status = static_cast<int>(value);
        }
        return step;
    }
    if (name == "break" || name == "continue") {
        step.flow = name == "break" ? Flow::Break : Flow::Continue;
        step.levels = 1;
        if (!operands.empty()) {
            std::size_t value = 0;
            if (!parse_count(operands[0], value) || value == 0) {
                step.flow = Flow::Normal;
                step.status = 2;
                return step;
            }
            step.levels = value;
        }
        return step;
    }
    if (name == "shift") {
        std::size_t count = 1;
        if (!operands.empty() && !parse_count(operands[0], count)) {
            step.status = 2;
            return step;
        }
        if (!state_.core().shift(count).ok()) step.status = 1;
        return step;
    }
    if (name == "set") {
        if (!operands.empty() && operands[0] == "--") {
            std::vector<std::string_view> replacement;
            for (std::size_t i = 1; i < operands.size(); ++i) {
                replacement.push_back(operands[i]);
            }
            const auto name0 = state_.core().positional(0);
            if (!state_.core()
                     .set_positionals(name0.value, replacement)
                     .ok()) {
                step.status = 1;
            }
            return step;
        }
        for (const auto& option : operands) {
            if (option.size() < 2 ||
                (option.front() != '-' && option.front() != '+')) {
                step.status = 2;
                return step;
            }
            for (std::size_t i = 1; i < option.size(); ++i) {
                if (option[i] != 'e' && option[i] != 'u') {
                    step.status = 2;
                    return step;
                }
            }
        }
        for (const auto& option : operands) {
            const auto enable = option.front() == '-';
            for (std::size_t i = 1; i < option.size(); ++i) {
                if (option[i] == 'e') state_.core().errexit = enable;
                if (option[i] == 'u') state_.core().nounset = enable;
            }
        }
        return step;
    }
    if (name == "export") {
        for (const auto& operand : operands) {
            const auto equal = operand.find('=');
            if (equal == std::string::npos) {
                state_.export_name(operand);
                continue;
            }
            const auto variable = operand.substr(0, equal);
            if (!state_.core()
                     .assign(variable, operand.substr(equal + 1))
                     .ok()) {
                step.status = 1;
                return step;
            }
            state_.export_name(variable);
        }
        return step;
    }
    if (name == "trap") {
        std::vector<std::string_view> arguments;
        arguments.push_back(name);
        for (const auto& operand : operands) arguments.push_back(operand);
        const auto configured = configure_trap(state_, arguments,
                                               services_.signal);
        if (!configured.ok()) {
            (void)emit(command.streams.error,
                       "trap: bad condition " +
                           configured.bad_condition + "\n");
            step.status = 1;
        }
        return step;
    }
    if (name == "read") {
        auto raw = false;
        std::vector<std::string> variables;
        for (const auto& operand : operands) {
            if (operand == "-r") {
                raw = true;
                continue;
            }
            variables.push_back(operand);
        }
        (void)raw;
        if (variables.empty() || command.streams.input == invalid_handle ||
            services_.io.read == nullptr) {
            step.status = 1;
            return step;
        }
        std::string line;
        std::byte byte{};
        for (;;) {
            std::size_t moved = 0;
            const auto status = services_.io.read(
                services_.io.context, command.streams.input,
                std::span<std::byte>{&byte, 1}, moved);
            if (status == ServiceStatus::Interrupted) continue;
            if (status != ServiceStatus::Ok || moved == 0) break;
            const auto value = static_cast<char>(byte);
            if (value == '\n') break;
            line.push_back(value);
        }
        if (line.empty()) {
            step.status = 1;
            return step;
        }
        // With IFS unset to empty the whole line reaches the first name, which
        // is the form the tracked corpus uses.
        const auto ifs = state_.core().ifs();
        if (ifs.empty() || variables.size() == 1) {
            if (!state_.core().assign(variables[0], line).ok()) {
                step.status = 1;
            }
            return step;
        }
        std::size_t at = 0;
        for (std::size_t i = 0; i < variables.size(); ++i) {
            while (at < line.size() &&
                   ifs.find(line[at]) != std::string_view::npos) {
                ++at;
            }
            const auto start = at;
            if (i + 1 == variables.size()) {
                at = line.size();
            } else {
                while (at < line.size() &&
                       ifs.find(line[at]) == std::string_view::npos) {
                    ++at;
                }
            }
            if (!state_.core()
                     .assign(variables[i], line.substr(start, at - start))
                     .ok()) {
                step.status = 1;
                return step;
            }
        }
        return step;
    }
    if (name == "command") {
        if (operands.empty()) {
            step.status = 2;
            return step;
        }
        if (operands[0] == "-v") {
            if (operands.size() != 2) {
                step.status = 2;
                return step;
            }
            if (!emit(command.streams.output, operands[1] + "\n")) {
                step.status = 1;
            }
            return step;
        }
        // command bypasses functions, so the remaining words run as an
        // ordinary command with this shell's builtins and then the host.
        Command inner = command;
        inner.arguments.assign(operands.begin(), operands.end());
        auto inner_handled = false;
        auto inner_step = builtin(inner, inner_handled);
        return inner_handled ? inner_step : external(inner);
    }
    if (name == "exec") {
        if (operands.empty()) {
            step.status = 2;
            return step;
        }
        // No service replaces this process image, so exec runs the program and
        // exits with its status. That is observationally the same for a script
        // whose last action is exec, which is the corpus's only use.
        Command inner = command;
        inner.arguments.assign(operands.begin(), operands.end());
        step = external(inner);
        step.flow = Flow::Exit;
        return step;
    }
    handled = false;
    return step;
}

}  // namespace mm::shell::full
