// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;

namespace {

// The fixed level-1 boundary script. It exercises assignment, a conditional, a
// loop, checked arithmetic, a function with a positional argument, command
// substitution, a custom native handler, an installed script, and yield.
constexpr std::string_view boundary_script =
    "count=0\n"
    "greet() {\n"
    "    echo greet $1\n"
    "}\n"
    "if test -n \"$count\"; then\n"
    "    greet one\n"
    "fi\n"
    "for item in a b; do\n"
    "    count=$((count + 1))\n"
    "    device $item\n"
    "done\n"
    "stamp=$(echo captured)\n"
    "echo stamp $stamp\n"
    "report\n"
    "yield\n"
    "echo count $count\n";

constexpr std::string_view report_script = "echo report $count\n";

constexpr std::string_view expected_output =
    "greet one\n"
    "device a\n"
    "device b\n"
    "stamp captured\n"
    "report 2\n"
    "count 2\n";

// The named acceptance storage profile. Every capacity is a declared array
// here; mm.shell allocates nothing. docs/modules-shell.mdy records the
// measured requirement beside each one.
alignas(void*) char source_storage[512];

alignas(void*) mm::shell::ScriptToken parse_tokens[64];
alignas(void*) mm::shell::WordFragment parse_fragments[48];
alignas(void*) mm::shell::SyntaxNode parse_nodes[72];
alignas(void*) mm::shell::SyntaxLink parse_links[72];
alignas(void*) mm::shell::ParserFrame parse_context[16];

alignas(void*) mm::shell::VariableSlot variables[16];
alignas(void*) char variable_text[256];
alignas(void*) mm::shell::PositionalSlot positionals[16];
alignas(void*) char positional_text[128];

alignas(void*) mm::shell::FieldPiece expansion_pieces[64];
alignas(void*) char expansion_generated[128];
alignas(void*) char expansion_field_text[128];
alignas(void*) mm::shell::SourceSpan expansion_fields[32];
alignas(void*) mm::shell::VariableSlot shadow_variables[16];
alignas(void*) char shadow_variable_text[256];

alignas(void*) std::string_view arguments[32];
alignas(void*) char argument_text[256];
alignas(void*) mm::shell::EvaluatorFrame frames[16];
alignas(void*) std::string_view loop_items[16];
alignas(void*) char loop_text[128];
alignas(void*) mm::shell::PatternByte pattern_bytes[64];
alignas(void*) mm::shell::VariableSlot prefix_variables[16];
alignas(void*) char prefix_variable_text[256];
alignas(void*) mm::shell::PositionalSlot call_positionals[32];

alignas(void*) char capture_text[256];
alignas(void*) std::string_view capture_values[8];
alignas(void*) mm::shell::VariableSlot capture_variables[16];
alignas(void*) char capture_variable_text[256];
alignas(void*) mm::shell::ScriptToken capture_tokens[32];
alignas(void*) mm::shell::WordFragment capture_fragments[32];
alignas(void*) mm::shell::SyntaxNode capture_nodes[32];
alignas(void*) mm::shell::SyntaxLink capture_links[32];
alignas(void*) mm::shell::ParserFrame capture_parser_context[16];

alignas(void*) mm::shell::FunctionSlot function_slots[4];
alignas(void*) char function_text[256];
alignas(void*) mm::shell::ScriptToken function_tokens[32];
alignas(void*) mm::shell::WordFragment function_fragments[32];
alignas(void*) mm::shell::SyntaxNode function_nodes[32];
alignas(void*) mm::shell::SyntaxLink function_links[32];
alignas(void*) mm::shell::ParserFrame function_context[16];

alignas(void*) mm::shell::FunctionSlot capture_function_slots[4];
alignas(void*) char capture_function_text[256];
alignas(void*) mm::shell::ScriptToken capture_function_tokens[32];
alignas(void*) mm::shell::WordFragment capture_function_fragments[32];
alignas(void*) mm::shell::SyntaxNode capture_function_nodes[32];
alignas(void*) mm::shell::SyntaxLink capture_function_links[32];
alignas(void*) mm::shell::ParserFrame capture_function_context[16];

alignas(void*) mm::shell::ScriptSlot script_slots[2];
alignas(void*) mm::shell::ScriptToken script_tokens[32];
alignas(void*) mm::shell::WordFragment script_fragments[32];
alignas(void*) mm::shell::SyntaxNode script_nodes[32];
alignas(void*) mm::shell::SyntaxLink script_links[32];
alignas(void*) mm::shell::ParserFrame script_context[16];

alignas(void*) mm::shell::CommandDescriptor command_storage[24];
alignas(void*) char staged_output_storage[256];
alignas(void*) char staged_error_storage[256];
alignas(void*) char application_output[512];
alignas(void*) std::byte transaction_scratch[256];

// Exit codes above this base name the storage class that came up short, so a
// capacity failure says which array to grow rather than only that it failed.
constexpr int storage_exit_base = 64;

[[nodiscard]] int storage_exit(mm::shell::StorageClass storage_class) {
    return storage_exit_base + static_cast<int>(storage_class);
}

// A fake bounded transaction: it preflights its whole working space before
// touching the device, then reports the pin it drove.
struct DeviceState {
    std::size_t required = 8;
    std::size_t effects = 0;
};

DeviceState device_state;

void device_handler(void* data, std::span<const std::string_view> args,
                    mm::shell::CommandContext& context,
                    mm::shell::CommandResult& result) {
    auto& state = *static_cast<DeviceState*>(data);
    if (context.scratch.size() < state.required) {
        result.status = 2;
        result.error = mm::shell::Status::Overflow;
        result.overflow = {mm::shell::StorageClass::TransactionScratch,
                           state.required};
        return;
    }
    for (std::size_t i = 0; i < state.required; ++i) {
        context.scratch[i] = static_cast<std::byte>(i);
    }
    ++state.effects;
    if (context.io.out.write(std::string_view{"device "}) !=
            mm::shell::SinkResult::Accepted ||
        context.io.out.write(args.size() > 1 ? args[1]
                                            : std::string_view{}) !=
            mm::shell::SinkResult::Accepted ||
        context.io.out.write(std::string_view{"\n"}) !=
            mm::shell::SinkResult::Accepted) {
        const auto failure = context.io.out.failure();
        result.status = 1;
        result.error = failure.error;
        result.overflow = failure.overflow;
    }
}

}  // namespace

int main() {
    if (boundary_script.size() > sizeof(source_storage)) return 10;
    for (std::size_t i = 0; i < boundary_script.size(); ++i) {
        source_storage[i] = boundary_script[i];
    }
    const auto source = mm::shell::SourceView{
        std::string_view{source_storage, boundary_script.size()}};

    // The parse requirement is measured before any storage is committed, so a
    // short array is reported rather than discovered mid-parse.
    const auto measured = mm::shell::measure_embedded(source);
    if (measured.status != mm::shell::ParseStatus::Complete) return 11;
    if (measured.required.tokens > sizeof(parse_tokens) /
                                      sizeof(parse_tokens[0]) ||
        measured.required.fragments > sizeof(parse_fragments) /
                                          sizeof(parse_fragments[0]) ||
        measured.required.nodes > sizeof(parse_nodes) /
                                     sizeof(parse_nodes[0]) ||
        measured.required.links > sizeof(parse_links) /
                                     sizeof(parse_links[0]) ||
        measured.required.context > sizeof(parse_context) /
                                        sizeof(parse_context[0])) {
        return 12;
    }

    mm::shell::EmbeddedScript script;
    const auto parsed = mm::shell::parse_embedded(
        source,
        {parse_tokens, parse_fragments, parse_nodes, parse_links,
         parse_context},
        script);
    if (parsed.status != mm::shell::ParseStatus::Complete) {
        return storage_exit(parsed.overflow.storage_class);
    }

    mm::shell::MemorySink output{application_output};
    mm::shell::MemorySink diagnostics{staged_error_storage};
    mm::shell::IoServices io{output.sink(), diagnostics.sink()};

    mm::shell::ShellState state{variables, variable_text, positionals,
                                positional_text};
    const std::string_view script_arguments[]{"boundary"};
    if (!state.set_positionals("shell-smoke", script_arguments).ok()) {
        return 13;
    }

    const auto capabilities = mm::shell::CapabilitySet::level1();
    mm::shell::CommandContext context{io, state, capabilities,
                                      transaction_scratch};

    mm::shell::Registry registry{command_storage};
    mm::shell::FunctionLibrary functions{
        {function_slots, function_text, function_tokens, function_fragments,
         function_nodes, function_links, function_context}};
    mm::shell::FunctionLibrary capture_functions{
        {capture_function_slots, capture_function_text,
         capture_function_tokens, capture_function_fragments,
         capture_function_nodes, capture_function_links,
         capture_function_context}};
    mm::shell::ScriptLibrary scripts{
        {script_slots, script_tokens, script_fragments, script_nodes,
         script_links, script_context}};
    mm::shell::Introspection binding{&registry, &scripts};

    // The standard pack is installed before any custom command, so a custom
    // name cannot take one of theirs.
    const auto standard = mm::shell::install_level1(registry, binding);
    if (!standard.ok()) {
        return standard.status == mm::shell::Status::Overflow
                   ? storage_exit(standard.overflow.storage_class)
                   : 14;
    }
    if (!registry.install({
                              .name = "device",
                              .summary = "drive a fake pin",
                              .command_class =
                                  mm::shell::CommandClass::Custom,
                              .required_capabilities = {},
                              .handler = &device_handler,
                              .context = &device_state,
                          })
             .ok()) {
        return 15;
    }
    if (!scripts.install({
                             .name = "report",
                             .summary = "report the loop count",
                             .required_capabilities = {},
                             .source =
                                 mm::shell::SourceView{report_script},
                         },
                         &registry)
             .ok()) {
        return 16;
    }

    mm::shell::EvaluatorStorage evaluator_storage{
        .expansion = {expansion_pieces, expansion_generated,
                      {expansion_field_text, expansion_fields},
                      shadow_variables, shadow_variable_text},
        .arguments = arguments,
        .argument_text = argument_text,
        .frames = frames,
        .loop_items = loop_items,
        .loop_text = loop_text,
        .pattern = pattern_bytes,
        .prefix_variables = prefix_variables,
        .prefix_variable_text = prefix_variable_text,
        .staged_output = staged_output_storage,
        .staged_error = staged_error_storage,
        .functions = &functions,
        .scripts = &scripts,
        .call_positionals = call_positionals,
        .capture_text = capture_text,
        .capture_values = capture_values,
        .capture_variables = capture_variables,
        .capture_variable_text = capture_variable_text,
        .capture_tokens = capture_tokens,
        .capture_fragments = capture_fragments,
        .capture_nodes = capture_nodes,
        .capture_links = capture_links,
        .capture_parser_context = capture_parser_context,
        .capture_functions = &capture_functions,
    };

    mm::shell::Evaluator evaluator;
    if (evaluator.begin(script, registry, context, evaluator_storage) !=
        mm::shell::Status::Ok) {
        return 17;
    }

    // The application pumps: it steps, and a yield hands control back here
    // between commands exactly as a console loop would see it.
    std::size_t yields = 0;
    std::size_t steps = 0;
    mm::shell::StepResult result;
    for (; steps < 4096; ++steps) {
        result = evaluator.step();
        if (result.step == mm::shell::Step::Yielded) {
            ++yields;
            continue;
        }
        if (result.step != mm::shell::Step::Running) break;
    }
    if (result.step == mm::shell::Step::Failed) {
        return result.command.error == mm::shell::Status::Overflow
                   ? storage_exit(result.command.overflow.storage_class)
                   : 18;
    }
    if (result.step != mm::shell::Step::Complete) return 19;
    if (result.command.status != 0) return 20;
    if (yields != 1) return 21;
    if (evaluator.frame_depth() != 0) return 22;
    if (device_state.effects != 2) return 23;
    if (output.view() != expected_output) return 24;
    if (diagnostics.written != 0) return 25;
    return 0;
}
