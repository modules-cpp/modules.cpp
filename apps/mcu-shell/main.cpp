// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;
import mm.shell.mcu;
import mm.mcu;
import mm.stdio;

namespace {

constexpr std::string_view blink_source =
    "gpio configure $1 out none\n"
    "gpio write $1 1 || return $?\n"
    "delay ms 100\n"
    "gpio write $1 0\n";

constexpr std::string_view adc_source =
    "adc configure $1 || return $?\n"
    "sample=$(adc read $1)\n"
    "echo $sample\n"
    "adc release $1\n";

struct Memory {
    char source[512]{};
    mm::shell::ScriptToken tokens[64]{};
    mm::shell::WordFragment fragments[64]{};
    mm::shell::SyntaxNode nodes[80]{};
    mm::shell::SyntaxLink links[80]{};
    mm::shell::ParserFrame parser_context[16]{};

    mm::shell::VariableSlot variables[32]{};
    char variable_text[512]{};
    mm::shell::PositionalSlot positionals[32]{};
    char positional_text[256]{};

    mm::shell::FieldPiece pieces[64]{};
    char generated[256]{};
    char field_text[512]{};
    mm::shell::SourceSpan fields[32]{};
    mm::shell::VariableSlot shadow_variables[32]{};
    char shadow_text[512]{};

    std::string_view arguments[32]{};
    char argument_text[512]{};
    mm::shell::EvaluatorFrame frames[24]{};
    std::string_view loop_items[16]{};
    char loop_text[256]{};
    mm::shell::PatternByte pattern[64]{};
    mm::shell::VariableSlot prefix_variables[32]{};
    char prefix_text[512]{};
    mm::shell::PositionalSlot call_positionals[48]{};

    char capture_text[256]{};
    std::string_view capture_values[8]{};
    mm::shell::VariableSlot capture_variables[32]{};
    char capture_variable_text[512]{};
    mm::shell::ScriptToken capture_tokens[32]{};
    mm::shell::WordFragment capture_fragments[32]{};
    mm::shell::SyntaxNode capture_nodes[32]{};
    mm::shell::SyntaxLink capture_links[32]{};
    mm::shell::ParserFrame capture_context[16]{};

    mm::shell::FunctionSlot function_slots[8]{};
    char function_text[1024]{};
    mm::shell::ScriptToken function_tokens[64]{};
    mm::shell::WordFragment function_fragments[64]{};
    mm::shell::SyntaxNode function_nodes[64]{};
    mm::shell::SyntaxLink function_links[64]{};
    mm::shell::ParserFrame function_context[16]{};

    mm::shell::FunctionSlot capture_function_slots[4]{};
    char capture_function_text[512]{};
    mm::shell::ScriptToken capture_function_tokens[32]{};
    mm::shell::WordFragment capture_function_fragments[32]{};
    mm::shell::SyntaxNode capture_function_nodes[32]{};
    mm::shell::SyntaxLink capture_function_links[32]{};
    mm::shell::ParserFrame capture_function_context[16]{};

    mm::shell::ScriptSlot script_slots[8]{};
    mm::shell::ScriptToken script_tokens[96]{};
    mm::shell::WordFragment script_fragments[96]{};
    mm::shell::SyntaxNode script_nodes[96]{};
    mm::shell::SyntaxLink script_links[96]{};
    mm::shell::ParserFrame script_context[16]{};

    mm::shell::CommandDescriptor commands[36]{};
    char staged_out[512]{};
    char staged_err[256]{};
    // RP2350B exposes up to 48 named GPIO/PWM entries: 256 is too small.
    std::byte transaction[512]{};
    char console_pending[512]{};
    std::byte console_input[64]{};
};

Memory memory;

[[nodiscard]] mm::shell::EvaluatorStorage evaluator_storage(
    mm::shell::FunctionLibrary& functions,
    mm::shell::FunctionLibrary& child_functions,
    mm::shell::ScriptLibrary& scripts) {
    return {
        .expansion = {memory.pieces, memory.generated,
                      {memory.field_text, memory.fields},
                      memory.shadow_variables, memory.shadow_text},
        .arguments = memory.arguments,
        .argument_text = memory.argument_text,
        .frames = memory.frames,
        .loop_items = memory.loop_items,
        .loop_text = memory.loop_text,
        .pattern = memory.pattern,
        .prefix_variables = memory.prefix_variables,
        .prefix_variable_text = memory.prefix_text,
        .staged_output = memory.staged_out,
        .staged_error = memory.staged_err,
        .functions = &functions,
        .scripts = &scripts,
        .call_positionals = memory.call_positionals,
        .capture_text = memory.capture_text,
        .capture_values = memory.capture_values,
        .capture_variables = memory.capture_variables,
        .capture_variable_text = memory.capture_variable_text,
        .capture_tokens = memory.capture_tokens,
        .capture_fragments = memory.capture_fragments,
        .capture_nodes = memory.capture_nodes,
        .capture_links = memory.capture_links,
        .capture_parser_context = memory.capture_context,
        .capture_functions = &child_functions,
    };
}

}  // namespace

int main() {
    mm::shell::mcu::McuConsole console;
    if (console.attach(mm::stdio::selected_console(),
                       memory.console_pending) !=
        mm::stdio::Status::Ok) return 1;

    mm::shell::ShellState state{
        memory.variables, memory.variable_text, memory.positionals,
        memory.positional_text};
    if (!state.set_positionals("mcu-shell", {}).ok()) return 2;

    auto active = mm::shell::mcu::capabilities(&console);
    mm::shell::IoServices io{console.sink(), console.sink()};
    mm::shell::CommandContext context{
        io, state, active, memory.transaction};
    mm::shell::Registry registry{memory.commands};
    mm::shell::FunctionLibrary functions{{
        memory.function_slots, memory.function_text,
        memory.function_tokens, memory.function_fragments,
        memory.function_nodes, memory.function_links,
        memory.function_context}};
    mm::shell::FunctionLibrary child_functions{{
        memory.capture_function_slots, memory.capture_function_text,
        memory.capture_function_tokens, memory.capture_function_fragments,
        memory.capture_function_nodes, memory.capture_function_links,
        memory.capture_function_context}};
    mm::shell::ScriptLibrary scripts{{
        memory.script_slots, memory.script_tokens,
        memory.script_fragments, memory.script_nodes,
        memory.script_links, memory.script_context}};
    mm::shell::Introspection introspection{&registry, &scripts};
    mm::shell::mcu::Level2Binding binding{&console};
    if (!mm::shell::mcu::install_level2(
             registry, introspection, binding).ok()) return 3;

    mm::shell::CapabilitySet blink_needs;
    blink_needs.set(mm::shell::Capability::Gpio);
    blink_needs.set(mm::shell::Capability::Timer);
    mm::shell::CapabilitySet sample_needs;
    sample_needs.set(mm::shell::Capability::Adc);
    const mm::shell::ScriptDescriptor builtins[]{
        {.name = "blink", .summary = "blink a GPIO once",
         .required_capabilities = blink_needs,
         .source = mm::shell::SourceView{blink_source}},
        {.name = "sample", .summary = "read an ADC channel",
         .required_capabilities = sample_needs,
         .source = mm::shell::SourceView{adc_source}},
    };
    if (!scripts.install_pack(builtins, &registry).ok()) return 4;

    mm::shell::Session session;
    const mm::shell::SessionStorage storage{
        .source = memory.source,
        .script = {memory.tokens, memory.fragments, memory.nodes,
                   memory.links, memory.parser_context},
        .evaluator = evaluator_storage(functions, child_functions,
                                       scripts)};
    if (session.begin(registry, context, storage) !=
        mm::shell::Status::Ok) return 5;

    std::size_t available = 0;
    std::size_t at = 0;
    for (;;) {
        if (session.executing()) {
            const auto result = session.step();
            if (result.step == mm::shell::Step::Failed) {
                (void)console.sink().write("shell: execution failed\n");
            }
        } else if (at < available) {
            const auto* bytes = reinterpret_cast<const char*>(
                memory.console_input + at);
            const auto feed = session.feed(
                std::span<const char>{bytes, available - at});
            at += feed.consumed;
            if (!feed.ok()) {
                (void)console.sink().write("shell: input error\n");
            }
            if (feed.consumed == 0) at = available;
        } else {
            available = 0;
            at = 0;
            const auto read = console.read(memory.console_input,
                                           available);
            if (read != mm::stdio::Status::Ok &&
                read != mm::stdio::Status::Busy) return 6;
            if (available > sizeof(memory.console_input)) return 7;
        }
        const auto pumped = console.pump();
        if (pumped != mm::stdio::Status::Ok &&
            pumped != mm::stdio::Status::Busy) return 8;
        (void)mm::mcu::delay_ms(1);
    }
}
