// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :execute;
import :expand;
import :function;
import :interpret;
import :parse;
import :redirect;
import :service;
import :state;
import :syntax;
import :word;
import mm.shell;

namespace mm::shell::full {
namespace {

constexpr unsigned int depth_limit = 64;

[[nodiscard]] Diagnostic complain(ParseStatus status, std::size_t offset,
                                  std::string_view message) {
    return {status, offset, std::string{message}};
}

[[nodiscard]] std::size_t child_count(const Node& node) {
    return node.children.size();
}

[[nodiscard]] bool is_assignment(std::string_view word) {
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

[[nodiscard]] std::string_view assignment_name(std::string_view word) {
    return word.substr(0, word.find('='));
}

[[nodiscard]] std::string_view assignment_value(std::string_view word) {
    return word.substr(word.find('=') + 1);
}

[[nodiscard]] bool special_builtin(std::string_view name) {
    return name == ":" || name == "break" || name == "continue" ||
           name == "exit" || name == "return" || name == "set" ||
           name == "shift" || name == "export" || name == "trap" ||
           name == "exec";
}

[[nodiscard]] bool parse_unsigned(std::string_view text, std::size_t& out) {
    if (text.empty()) return false;
    out = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        out = out * 10 + static_cast<std::size_t>(c - '0');
    }
    return true;
}

// test and [ over the level-1 operator set, which is what the corpus uses.
[[nodiscard]] int evaluate_test(std::span<const std::string> operands,
                                bool& usage) {
    usage = false;
    if (operands.empty()) return 1;
    if (operands[0] == "!") {
        const auto inner = evaluate_test(operands.subspan(1), usage);
        if (usage) return 2;
        return inner == 0 ? 1 : 0;
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

[[nodiscard]] std::string printf_escape(std::string_view format,
                                        std::span<const std::string> operands,
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

Interpreter::Interpreter(FullState& state, Services services)
    : state_(state), services_(services) {}

void Interpreter::set_streams(Streams streams) { streams_ = streams; }

Status Interpreter::set_arguments(
    std::string_view name, std::span<const std::string_view> arguments) {
    return state_.core().set_positionals(name, arguments).status;
}

bool Interpreter::emit(Handle stream, std::string_view text) {
    if (stream == invalid_handle || services_.io.write == nullptr) {
        return false;
    }
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t moved = 0;
        const std::span<const std::byte> bytes{
            reinterpret_cast<const std::byte*>(text.data() + at),
            text.size() - at};
        const auto status = services_.io.write(services_.io.context, stream,
                                              bytes, moved);
        if (status == ServiceStatus::Interrupted ||
            status == ServiceStatus::WouldBlock) {
            continue;
        }
        if (status != ServiceStatus::Ok || moved == 0) return false;
        at += moved;
    }
    return true;
}

// Command substitution runs the inner source through this same interpreter
// with its ordinary output captured, so a nested list sees one evaluator.
ServiceStatus Interpreter::substitute(void* context, std::string_view source,
                                      std::string& out, int& status) {
    auto& self = *static_cast<Interpreter*>(context);
    if (self.services_.io.pipe == nullptr) return ServiceStatus::Invalid;
    if (self.depth_ >= depth_limit) return ServiceStatus::Failed;

    Handle read_end = invalid_handle;
    Handle write_end = invalid_handle;
    const auto piped = self.services_.io.pipe(self.services_.io.context,
                                              read_end, write_end);
    if (piped != ServiceStatus::Ok) return piped;

    // A substitution's mutations do not reach the parent, so it runs against a
    // forked state with its own host metadata.
    FullState child;
    if (self.state_.fork_into(child) != Status::Ok) {
        (void)self.services_.io.close(self.services_.io.context, read_end);
        (void)self.services_.io.close(self.services_.io.context, write_end);
        return ServiceStatus::Failed;
    }
    Interpreter nested{child, self.services_};
    nested.depth_ = self.depth_ + 1;
    nested.streams_ = {self.streams_.input, write_end, self.streams_.error};
    const auto adopted = nested.functions_.adopt_from(self.functions_);
    (void)adopted;

    const auto outcome = nested.run_text(source);
    self.spawns_ += nested.spawns_;
    for (const auto& record : nested.externals_) {
        self.externals_.push_back(record);
    }
    (void)self.services_.io.close(self.services_.io.context, write_end);

    // The whole capture is read before the status is reported, so a child that
    // wrote more than one pipe buffer is not truncated.
    out.clear();
    std::byte buffer[512];
    for (;;) {
        std::size_t moved = 0;
        const auto read = self.services_.io.read(
            self.services_.io.context, read_end,
            std::span<std::byte>{buffer, sizeof(buffer)}, moved);
        if (read == ServiceStatus::Interrupted) continue;
        if (read != ServiceStatus::Ok) break;
        if (moved == 0) break;
        out.append(reinterpret_cast<const char*>(buffer), moved);
    }
    (void)self.services_.io.close(self.services_.io.context, read_end);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    status = outcome.status;
    return outcome.service;
}

bool Interpreter::expand_fields(const FullScript& script, std::size_t token,
                                bool split, bool pathname,
                                std::vector<std::string>& out,
                                Step& failure) {
    const Substituter substituter{this, &Interpreter::substitute};
    const auto spelling = script.text(script.tokens[token].source);
    const auto fields = expand_word_full(
        spelling, state_.core(), {split, pathname, state_.directory()},
        services_.file, substituter);
    if (!fields.ok()) {
        failure.status = 1;
        failure.service = fields.service;
        if (fields.status == Status::NotFound && state_.core().nounset) {
            failure.flow = Flow::Exit;
        }
        failure.diagnostic = complain(
            fields.status == Status::Unsupported ? ParseStatus::Unsupported
                                               : ParseStatus::Malformed,
            script.tokens[token].source.offset + fields.issue,
            fields.status == Status::NotFound
                ? "parameter not set"
                : "word expansion failed");
        return false;
    }
    for (const auto& field : fields.fields) out.push_back(field);
    last_substitution_ = fields.substitution_status;
    return true;
}

bool Interpreter::apply_redirections(const FullScript& script,
                                     std::size_t node, Command& command,
                                     Step& failure) {
    const auto plan = plan_redirections(script, node);
    if (!plan.ok()) {
        failure.status = 2;
        failure.diagnostic = plan.diagnostic;
        return false;
    }
    for (const auto& step : plan.steps) {
        if (step.kind == RedirectionKind::HereDocument) {
            Handle read_end = invalid_handle;
            Handle write_end = invalid_handle;
            if (services_.io.pipe == nullptr ||
                services_.io.pipe(services_.io.context, read_end,
                                  write_end) != ServiceStatus::Ok) {
                failure.status = 1;
                failure.service = ServiceStatus::Failed;
                return false;
            }
            const auto body = expand_here_document(step.body,
                                                   step.expand_body,
                                                   state_.core());
            if (body.status != Status::Ok) {
                (void)services_.io.close(services_.io.context, read_end);
                (void)services_.io.close(services_.io.context, write_end);
                failure.status = 1;
                failure.diagnostic = complain(ParseStatus::Malformed,
                                              body.issue,
                                              "here-document expansion");
                return false;
            }
            for (const auto& segment : body.segments) {
                if (segment.kind == HereSegmentKind::Literal) {
                    if (!emit(write_end, segment.text)) break;
                    continue;
                }
                std::string captured;
                int status = 0;
                if (substitute(this, segment.text, captured, status) !=
                    ServiceStatus::Ok) {
                    break;
                }
                if (!emit(write_end, captured)) break;
            }
            (void)services_.io.close(services_.io.context, write_end);
            command.opened.push_back(read_end);
            command.streams.input = read_end;
            continue;
        }
        std::vector<std::string> target;
        // A redirection operand expands but is neither split nor matched.
        const Substituter substituter{this, &Interpreter::substitute};
        const auto expanded = expand_word_full(
            step.operand, state_.core(), {false, false, state_.directory()},
            services_.file, substituter);
        if (!expanded.ok()) {
            failure.status = 1;
            failure.service = expanded.service;
            failure.diagnostic = complain(ParseStatus::Malformed, 0,
                                          "redirection operand");
            return false;
        }
        const auto operand = expanded.fields.empty() ? std::string{}
                                                     : expanded.fields[0];
        if (step.kind == RedirectionKind::DuplicateOutput ||
            step.kind == RedirectionKind::DuplicateInput) {
            // Only the two standard streams are named by the corpus, and a
            // descriptor the shell does not hold cannot be duplicated.
            if (operand == "1") {
                command.streams.error = command.streams.output;
            } else if (operand == "2") {
                command.streams.output = command.streams.error;
            } else if (operand == "-") {
                if (step.target == 0) {
                    command.streams.input = invalid_handle;
                } else if (step.target == 2) {
                    command.streams.error = invalid_handle;
                } else {
                    command.streams.output = invalid_handle;
                }
            } else {
                failure.status = 1;
                failure.diagnostic = complain(
                    ParseStatus::Unsupported, 0,
                    "only 1, 2, and - may be duplicated at this level");
                return false;
            }
            continue;
        }
        const auto mode = step.kind == RedirectionKind::Input
                              ? OpenMode::Read
                              : step.kind == RedirectionKind::Append
                                    ? OpenMode::Append
                                    : OpenMode::Truncate;
        Handle opened = invalid_handle;
        if (services_.io.open == nullptr) {
            failure.status = 1;
            failure.service = ServiceStatus::Invalid;
            return false;
        }
        const auto status = services_.io.open(services_.io.context, operand,
                                              mode, opened);
        if (status != ServiceStatus::Ok) {
            failure.status = 1;
            failure.service = ServiceStatus::Ok;
            failure.diagnostic = complain(ParseStatus::Malformed, 0,
                                          "cannot open " + operand);
            return false;
        }
        command.opened.push_back(opened);
        if (step.kind == RedirectionKind::Input) {
            command.streams.input = opened;
        } else if (step.target == 2) {
            command.streams.error = opened;
        } else {
            command.streams.output = opened;
        }
    }
    return true;
}

void Interpreter::release(Command& command) {
    for (const auto handle : command.opened) {
        if (services_.io.close != nullptr) {
            (void)services_.io.close(services_.io.context, handle);
        }
    }
    command.opened.clear();
}

}  // namespace mm::shell::full
