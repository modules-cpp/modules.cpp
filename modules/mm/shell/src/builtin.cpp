// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module mm.shell;

import :arithmetic;
import :builtin;
import :capability;
import :command;
import :io;
import :state;
import :status;

namespace mm::shell {
namespace {

// A sink that cannot accept bytes right now reports Unavailable rather than a
// broken sink. Staged output with retry arrives with Session; until then a
// handler stops at the first refusal instead of partially re-writing.
[[nodiscard]] bool emit(const ByteSink& sink, std::string_view text,
                        CommandResult& result) {
    const auto written = sink.write(text);
    if (written == SinkResult::Accepted) return true;
    const auto failure = sink.failure();
    result.status = static_cast<int>(CommandStatus::Failure);
    result.overflow = failure.overflow;
    if (written == SinkResult::WouldBlock) {
        result.error = Status::Unavailable;
        return false;
    }
    result.error = failure.error == Status::Ok ? Status::WriteError
                                              : failure.error;
    return false;
}

[[nodiscard]] bool emit(const ByteSink& sink, char byte,
                        CommandResult& result) {
    return emit(sink, std::string_view{&byte, 1}, result);
}

void usage(CommandResult& result, Status error = Status::BadArgument) {
    result.status = static_cast<int>(CommandStatus::Usage);
    result.error = error;
}

void colon_handler(void*, std::span<const std::string_view>,
                   CommandContext&, CommandResult&) {}

void true_handler(void*, std::span<const std::string_view>,
                  CommandContext&, CommandResult&) {}

void false_handler(void*, std::span<const std::string_view>,
                   CommandContext&, CommandResult& result) {
    result.status = static_cast<int>(CommandStatus::Failure);
}

void echo_handler(void*, std::span<const std::string_view> args,
                  CommandContext& context, CommandResult& result) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (i != 1 && !emit(context.io.out, ' ', result)) return;
        if (!emit(context.io.out, args[i], result)) return;
    }
    (void)emit(context.io.out, '\n', result);
}

[[nodiscard]] bool escape_byte(char spelling, char& byte) {
    switch (spelling) {
        case 'n': byte = '\n'; return true;
        case 't': byte = '\t'; return true;
        case 'r': byte = '\r'; return true;
        case 'a': byte = '\a'; return true;
        case 'b': byte = '\b'; return true;
        case 'f': byte = '\f'; return true;
        case 'v': byte = '\v'; return true;
        case '\\': byte = '\\'; return true;
        default: byte = spelling; return false;
    }
}

void printf_handler(void*, std::span<const std::string_view> args,
                    CommandContext& context, CommandResult& result) {
    if (args.size() < 2) {
        usage(result);
        return;
    }
    const auto format = args[1];
    auto next = std::size_t{2};
    for (;;) {
        const auto consumed = next;
        for (std::size_t i = 0; i < format.size(); ++i) {
            if (format[i] == '\\' && i + 1 < format.size()) {
                char byte = 0;
                if (!escape_byte(format[i + 1], byte) &&
                    !emit(context.io.out, '\\', result)) {
                    return;
                }
                ++i;
                if (!emit(context.io.out, byte, result)) return;
                continue;
            }
            if (format[i] == '%' && i + 1 < format.size()) {
                ++i;
                if (format[i] == '%') {
                    if (!emit(context.io.out, '%', result)) return;
                    continue;
                }
                if (format[i] != 's') {
                    result.status = static_cast<int>(CommandStatus::Failure);
                    result.error = Status::Unsupported;
                    return;
                }
                auto value = std::string_view{};
                if (next < args.size()) value = args[next++];
                if (!emit(context.io.out, value, result)) return;
                continue;
            }
            if (!emit(context.io.out, format[i], result)) return;
        }
        // The format is reused while conversions keep consuming operands.
        if (next >= args.size() || next == consumed) return;
    }
}

[[nodiscard]] bool numeric_operator(std::string_view spelling,
                                    NumericOperator& operation) {
    if (spelling == "-eq") operation = NumericOperator::Eq;
    else if (spelling == "-ne") operation = NumericOperator::Ne;
    else if (spelling == "-lt") operation = NumericOperator::Lt;
    else if (spelling == "-le") operation = NumericOperator::Le;
    else if (spelling == "-gt") operation = NumericOperator::Gt;
    else if (spelling == "-ge") operation = NumericOperator::Ge;
    else return false;
    return true;
}

[[nodiscard]] int test_expression(std::span<const std::string_view> operands,
                                  Status& error) {
    error = Status::Ok;
    if (operands.empty()) return static_cast<int>(CommandStatus::Failure);
    if (!operands.empty() && operands[0] == "!") {
        const auto inner = test_expression(operands.subspan(1), error);
        if (error != Status::Ok) return static_cast<int>(CommandStatus::Usage);
        return inner == 0 ? 1 : 0;
    }
    if (operands.size() == 1) return operands[0].empty() ? 1 : 0;
    if (operands.size() == 2) {
        if (operands[0] == "-n") return operands[1].empty() ? 1 : 0;
        if (operands[0] == "-z") return operands[1].empty() ? 0 : 1;
        error = Status::Unsupported;
        return static_cast<int>(CommandStatus::Usage);
    }
    if (operands.size() == 3) {
        if (operands[1] == "=") return operands[0] == operands[2] ? 0 : 1;
        if (operands[1] == "!=") return operands[0] != operands[2] ? 0 : 1;
        auto operation = NumericOperator::Eq;
        if (!numeric_operator(operands[1], operation)) {
            error = Status::Unsupported;
            return static_cast<int>(CommandStatus::Usage);
        }
        const auto compared = numeric_test(operation, operands[0],
                                           operands[2]);
        if (compared.status != ArithmeticStatus::Ok) {
            error = Status::BadArgument;
            return static_cast<int>(CommandStatus::Usage);
        }
        return compared.value ? 0 : 1;
    }
    error = Status::Unsupported;
    return static_cast<int>(CommandStatus::Usage);
}

void test_handler(void*, std::span<const std::string_view> args,
                  CommandContext&, CommandResult& result) {
    auto operands = args.subspan(1);
    if (args[0] == "[") {
        if (operands.empty() || operands.back() != "]") {
            usage(result);
            return;
        }
        operands = operands.first(operands.size() - 1);
    }
    auto error = Status::Ok;
    result.status = test_expression(operands, error);
    result.error = error;
}

void set_handler(void*, std::span<const std::string_view> args,
                 CommandContext& context, CommandResult& result) {
    // Options are validated before any flag changes, so a rejected operand
    // leaves every flag as it was.
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto option = args[i];
        if (option.size() < 2 ||
            (option[0] != '-' && option[0] != '+')) {
            usage(result);
            return;
        }
        for (std::size_t j = 1; j < option.size(); ++j) {
            if (option[j] != 'e' && option[j] != 'u') {
                usage(result, Status::Unsupported);
                return;
            }
        }
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto option = args[i];
        const auto enable = option[0] == '-';
        for (std::size_t j = 1; j < option.size(); ++j) {
            if (option[j] == 'e') context.state.errexit = enable;
            if (option[j] == 'u') context.state.nounset = enable;
        }
    }
}

void shift_handler(void*, std::span<const std::string_view> args,
                   CommandContext& context, CommandResult& result) {
    if (args.size() > 2) {
        usage(result);
        return;
    }
    auto count = std::size_t{1};
    if (args.size() == 2) {
        const auto parsed = parse_decimal(args[1]);
        if (!parsed.ok() || parsed.value < 0 ||
            parsed.value > static_cast<std::int64_t>(UINT32_MAX)) {
            usage(result);
            return;
        }
        count = static_cast<std::size_t>(parsed.value);
    }
    if (!context.state.shift(count).ok()) {
        result.status = static_cast<int>(CommandStatus::Failure);
    }
}

// A level-1 break or continue leaves exactly one loop; the language has no
// loop-count operand.
void break_handler(void*, std::span<const std::string_view> args,
                   CommandContext&, CommandResult& result) {
    if (args.size() != 1) {
        usage(result, Status::Unsupported);
        return;
    }
    result.flow = Flow::Break;
}

void continue_handler(void*, std::span<const std::string_view> args,
                      CommandContext&, CommandResult& result) {
    if (args.size() != 1) {
        usage(result, Status::Unsupported);
        return;
    }
    result.flow = Flow::Continue;
}

[[nodiscard]] bool exit_status(std::span<const std::string_view> args,
                               CommandResult& result) {
    if (args.size() == 1) return true;
    if (args.size() != 2) {
        usage(result);
        return false;
    }
    const auto parsed = parse_decimal(args[1]);
    if (!parsed.ok() || parsed.value < 0 || parsed.value > 255) {
        usage(result);
        return false;
    }
    result.status = static_cast<int>(parsed.value);
    return true;
}

void return_handler(void*, std::span<const std::string_view> args,
                    CommandContext& context, CommandResult& result) {
    result.status = context.state.last_status;
    if (!exit_status(args, result)) return;
    result.flow = Flow::Return;
}

void exit_handler(void*, std::span<const std::string_view> args,
                  CommandContext& context, CommandResult& result) {
    result.status = context.state.last_status;
    if (!exit_status(args, result)) return;
    result.flow = Flow::Exit;
}

void yield_handler(void*, std::span<const std::string_view> args,
                   CommandContext&, CommandResult& result) {
    if (args.size() != 1) {
        usage(result);
        return;
    }
    result.flow = Flow::Yield;
}

void capability_handler(void*, std::span<const std::string_view> args,
                        CommandContext& context, CommandResult& result) {
    if (args.size() == 1) {
        for (const auto& entry : detail::capability_names) {
            if (!context.capabilities.has(entry.capability)) continue;
            if (!emit(context.io.out, entry.name, result)) return;
            if (!emit(context.io.out, '\n', result)) return;
        }
        return;
    }
    if (args.size() != 2) {
        usage(result);
        return;
    }
    const auto found = lookup_capability(args[1]);
    if (!found.has_value()) {
        usage(result);
        return;
    }
    if (!context.capabilities.has(*found)) {
        result.status = static_cast<int>(CommandStatus::Failure);
    }
}

[[nodiscard]] bool describe(const ByteSink& sink,
                            const CommandDescriptor& descriptor,
                            CommandResult& result) {
    if (!emit(sink, descriptor.name, result)) return false;
    if (descriptor.summary.empty()) return emit(sink, '\n', result);
    return emit(sink, ' ', result) &&
           emit(sink, descriptor.summary, result) &&
           emit(sink, '\n', result);
}

void help_handler(void* data, std::span<const std::string_view> args,
                  CommandContext& context, CommandResult& result) {
    auto* registry = static_cast<Registry*>(data);
    if (registry == nullptr) {
        result.status = static_cast<int>(CommandStatus::Unavailable);
        result.error = Status::Unavailable;
        return;
    }
    if (args.size() == 1) {
        for (const auto& descriptor : registry->descriptors()) {
            if (!describe(context.io.out, descriptor, result)) return;
        }
        return;
    }
    if (args.size() != 2) {
        usage(result);
        return;
    }
    const auto* found = registry->find(args[1]);
    if (found == nullptr) {
        result.status = static_cast<int>(CommandStatus::Failure);
        result.error = Status::NotFound;
        return;
    }
    (void)describe(context.io.out, *found, result);
}

void command_handler(void* data, std::span<const std::string_view> args,
                     CommandContext& context, CommandResult& result) {
    auto* registry = static_cast<Registry*>(data);
    if (registry == nullptr) {
        result.status = static_cast<int>(CommandStatus::Unavailable);
        result.error = Status::Unavailable;
        return;
    }
    auto index = std::size_t{1};
    auto verbose = false;
    if (index < args.size() && args[index] == "-v") {
        verbose = true;
        ++index;
    }
    if (index >= args.size()) {
        usage(result);
        return;
    }
    const auto* found = registry->find(args[index]);
    if (found == nullptr) {
        result.status = static_cast<int>(CommandStatus::NotFound);
        result.error = Status::NotFound;
        return;
    }
    if (verbose) {
        if (!emit(context.io.out, found->name, result)) return;
        (void)emit(context.io.out, '\n', result);
        return;
    }
    // Function lookup arrives with change set 8; until then this is exactly
    // the registry dispatch the evaluator would have performed.
    result = dispatch(*found, args.subspan(index), context);
}

struct BuiltinEntry {
    std::string_view name;
    std::string_view summary;
    CommandClass command_class = CommandClass::Builtin;
    CommandHandler handler = nullptr;
    bool needs_registry = false;
};

constexpr BuiltinEntry entries[]{
    {":", "succeed without effect", CommandClass::SpecialBuiltin,
     &colon_handler},
    {"true", "succeed", CommandClass::Builtin, &true_handler},
    {"false", "fail with status 1", CommandClass::Builtin, &false_handler},
    {"echo", "write arguments and one newline", CommandClass::Builtin,
     &echo_handler},
    {"printf", "write a format with %s and escapes", CommandClass::Builtin,
     &printf_handler},
    {"test", "evaluate an embedded test expression", CommandClass::Builtin,
     &test_handler},
    {"[", "evaluate an embedded test expression", CommandClass::Builtin,
     &test_handler},
    {"set", "set or clear -e and -u", CommandClass::SpecialBuiltin,
     &set_handler},
    {"shift", "drop leading positional parameters",
     CommandClass::SpecialBuiltin, &shift_handler},
    {"break", "leave the innermost loop", CommandClass::SpecialBuiltin,
     &break_handler},
    {"continue", "restart the innermost loop", CommandClass::SpecialBuiltin,
     &continue_handler},
    {"return", "leave a function or installed script",
     CommandClass::SpecialBuiltin, &return_handler},
    {"exit", "end the evaluator", CommandClass::SpecialBuiltin,
     &exit_handler},
    {"yield", "return to the application between commands",
     CommandClass::Builtin, &yield_handler},
    {"capability", "list or query shell capabilities",
     CommandClass::Builtin, &capability_handler},
    {"help", "describe installed commands", CommandClass::Builtin,
     &help_handler, true},
    {"command", "resolve or run a registry command", CommandClass::Builtin,
     &command_handler, true},
};

static_assert(sizeof(entries) / sizeof(entries[0]) == core_builtin_count,
              "the descriptor table and its published count must agree");

}  // namespace

InstallResult core_builtins(Registry& registry,
                           std::span<CommandDescriptor> slots,
                           std::size_t& count) {
    count = 0;
    if (slots.size() < core_builtin_count) {
        return InstallResult{
            .status = Status::Overflow,
            .overflow = OverflowInfo{
                .storage_class = StorageClass::CustomCommands,
                .required = core_builtin_count,
            },
        };
    }
    for (const auto& entry : entries) {
        slots[count++] = CommandDescriptor{
            .name = entry.name,
            .summary = entry.summary,
            .command_class = entry.command_class,
            .required_capabilities = {},
            .handler = entry.handler,
            .context = entry.needs_registry ? &registry : nullptr,
        };
    }
    return InstallResult{};
}

}  // namespace mm::shell
