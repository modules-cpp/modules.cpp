// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::CommandContext;
using mm::shell::CommandDescriptor;
using mm::shell::CommandResult;
using mm::shell::Flow;
using mm::shell::Registry;
using mm::shell::Status;
using mm::test::expect;

// The core pack owns SpecialBuiltin descriptors, which Registry::install
// refuses by design. Focused tests therefore dispatch descriptors directly;
// the atomic install_level1 entry point arrives with change set 8.
struct Fixture {
    CommandDescriptor slots[mm::shell::core_builtin_count + 1]{};
    std::size_t count = 0;
    CommandDescriptor visible[4]{};
    Registry registry{visible};
    mm::shell::ScriptSlot script_slots[2]{};
    mm::shell::ScriptToken script_tokens[32]{};
    mm::shell::WordFragment script_fragments[32]{};
    mm::shell::SyntaxNode script_nodes[32]{};
    mm::shell::SyntaxLink script_links[32]{};
    mm::shell::ParserFrame script_context[16]{};
    mm::shell::ScriptLibrary scripts{
        {script_slots, script_tokens, script_fragments, script_nodes,
         script_links, script_context}};
    mm::shell::Introspection binding{&registry, &scripts};
    mm::shell::VariableSlot variables[8]{};
    char variable_text[128]{};
    mm::shell::PositionalSlot positionals[8]{};
    char positional_text[128]{};
    mm::shell::ShellState state{variables, variable_text, positionals,
                                positional_text};
    char out_bytes[256]{};
    char err_bytes[64]{};
    mm::shell::MemorySink output{out_bytes};
    mm::shell::MemorySink error{err_bytes};
    mm::shell::IoServices io{output.sink(), error.sink()};
    mm::shell::CapabilitySet capabilities =
        mm::shell::CapabilitySet::level1();
    std::byte scratch[8]{};
    CommandContext context{io, state, capabilities, scratch};

    Fixture() {
        expect(mm::shell::core_builtins(binding, slots, count).ok(),
               "core builtins materialize");
        expect(count == mm::shell::core_builtin_count,
               "every core builtin is published");
    }

    [[nodiscard]] const CommandDescriptor* find(std::string_view name) const {
        for (std::size_t i = 0; i < count; ++i) {
            if (slots[i].name == name) return &slots[i];
        }
        return nullptr;
    }

    [[nodiscard]] CommandResult run(
        std::span<const std::string_view> args) {
        const auto* descriptor = find(args[0]);
        if (descriptor == nullptr) {
            return {.status = 127, .error = Status::NotFound};
        }
        return mm::shell::dispatch(*descriptor, args, context);
    }

    [[nodiscard]] std::string_view written() const { return output.view(); }
};

void publishes_the_core_pack() {
    Fixture fixture;
    const std::string_view special[]{":",     "break", "continue", "exit",
                                     "return", "set",   "shift"};
    auto specials = 0;
    for (std::size_t i = 0; i < fixture.count; ++i) {
        if (fixture.slots[i].command_class ==
            mm::shell::CommandClass::SpecialBuiltin) {
            ++specials;
        }
        expect(fixture.slots[i].handler != nullptr,
               "every core descriptor has a handler");
        expect(!fixture.slots[i].summary.empty(),
               "every core descriptor is documented");
    }
    expect(specials == static_cast<int>(sizeof(special) /
                                        sizeof(special[0])),
           "exactly the POSIX level-1 special builtins are tagged");
    for (const auto name : special) {
        const auto* descriptor = fixture.find(name);
        expect(descriptor != nullptr &&
                   descriptor->command_class ==
                       mm::shell::CommandClass::SpecialBuiltin,
               "each named special builtin carries its class");
    }
    CommandDescriptor narrow[mm::shell::core_builtin_count - 1]{};
    std::size_t partial = 0;
    const auto refused = mm::shell::core_builtins(fixture.binding, narrow,
                                                  partial);
    expect(refused.status == Status::Overflow && partial == 0 &&
               refused.overflow.storage_class ==
                   mm::shell::StorageClass::CustomCommands,
           "a short descriptor span publishes nothing");
}

void trivial_status_commands() {
    Fixture fixture;
    const std::string_view colon[]{":"};
    const std::string_view yes[]{"true"};
    const std::string_view no[]{"false"};
    expect(fixture.run(colon).status == 0 && fixture.run(yes).status == 0 &&
               fixture.run(no).status == 1,
           ": and true succeed while false fails");
}

void echo_and_printf_write_bytes() {
    Fixture fixture;
    const std::string_view echo[]{"echo", "one", "two"};
    expect(fixture.run(echo).status == 0 &&
               fixture.written() == "one two\n",
           "echo joins arguments with one space and one newline");

    Fixture bare;
    const std::string_view empty[]{"echo"};
    expect(bare.run(empty).status == 0 && bare.written() == "\n",
           "echo with no operand writes only the newline");

    Fixture formatted;
    const std::string_view print[]{"printf", "%s=%s\\n", "key", "value"};
    expect(formatted.run(print).status == 0 &&
               formatted.written() == "key=value\n",
           "printf expands %s and a newline escape");

    Fixture reused;
    const std::string_view repeat[]{"printf", "[%s]", "a", "b", "c"};
    expect(reused.run(repeat).status == 0 &&
               reused.written() == "[a][b][c]",
           "the format is reused while operands remain");

    Fixture literal;
    const std::string_view percent[]{"printf", "100%%\\t\\\\"};
    expect(literal.run(percent).status == 0 &&
               literal.written() == "100%\t\\",
           "printf writes %% and backslash escapes literally");

    Fixture missing;
    const std::string_view short_args[]{"printf", "%s|%s", "only"};
    expect(missing.run(short_args).status == 0 &&
               missing.written() == "only|",
           "a missing %s operand expands to nothing");

    Fixture unsupported;
    const std::string_view numeric[]{"printf", "%d", "1"};
    const auto refused = unsupported.run(numeric);
    expect(refused.status == 1 && refused.error == Status::Unsupported,
           "an unimplemented conversion fails instead of guessing");
}

void full_sink_fails_without_partial_retry() {
    Fixture fixture;
    char tiny[2]{};
    mm::shell::MemorySink small{tiny};
    mm::shell::IoServices narrow{small.sink(), fixture.error.sink()};
    CommandContext context{narrow, fixture.state, fixture.capabilities,
                           fixture.scratch};
    const std::string_view echo[]{"echo", "abcdef"};
    const auto* descriptor = fixture.find("echo");
    const auto result = mm::shell::dispatch(*descriptor, echo, context);
    expect(result.status == 1 && result.error == Status::Overflow &&
               result.overflow.storage_class ==
                   mm::shell::StorageClass::StagedOutput,
           "a refused write reports the sink's own overflow");
}

struct TestVector {
    std::string_view args[5];
    std::size_t count = 0;
    int status = 0;
};

void embedded_test_operators() {
    Fixture fixture;
    const TestVector vectors[]{
        {{"test", "text"}, 2, 0},
        {{"test", ""}, 2, 1},
        {{"test", "-n", "text"}, 3, 0},
        {{"test", "-z", ""}, 3, 0},
        {{"test", "-z", "text"}, 3, 1},
        {{"test", "!", "-z", "text"}, 4, 0},
        {{"test", "a", "=", "a"}, 4, 0},
        {{"test", "a", "!=", "a"}, 4, 1},
        {{"test", "2", "-eq", "2"}, 4, 0},
        {{"test", "2", "-lt", "10"}, 4, 0},
        {{"test", "2", "-gt", "10"}, 4, 1},
        {{"test", "-9223372036854775808", "-lt", "0"}, 4, 0},
        {{"test"}, 1, 1},
    };
    for (const auto& vector : vectors) {
        const auto result = fixture.run(
            std::span<const std::string_view>{vector.args, vector.count});
        expect(result.status == vector.status &&
                   result.error == Status::Ok,
               "embedded test operator matches its expected status");
    }
    const std::string_view bracket[]{"[", "1", "-eq", "1", "]"};
    expect(fixture.run(bracket).status == 0, "[ accepts its closing ]");
    const std::string_view unclosed[]{"[", "1", "-eq", "1"};
    expect(fixture.run(unclosed).status == 2, "[ requires its closing ]");
    const std::string_view unknown[]{"test", "a", "-like", "b"};
    const auto refused = fixture.run(unknown);
    expect(refused.status == 2 && refused.error == Status::Unsupported,
           "an unknown test operator is a usage error");
    const std::string_view unnumeric[]{"test", "x", "-eq", "1"};
    expect(fixture.run(unnumeric).status == 2,
           "a non-numeric operand of -eq is a usage error");
}

void set_and_shift_are_transactional() {
    Fixture fixture;
    const std::string_view combined[]{"set", "-eu"};
    expect(fixture.run(combined).status == 0 && fixture.state.errexit &&
               fixture.state.nounset,
           "combined -eu enables both flags");
    const std::string_view clear[]{"set", "+e"};
    expect(fixture.run(clear).status == 0 && !fixture.state.errexit &&
               fixture.state.nounset,
           "+e clears only errexit");
    const std::string_view mixed[]{"set", "-u", "-x"};
    const auto refused = fixture.run(mixed);
    expect(refused.status == 2 && refused.error == Status::Unsupported &&
               !fixture.state.errexit,
           "an unsupported option leaves every flag unchanged");

    const std::string_view arguments[]{"one", "two", "three"};
    expect(fixture.state.set_positionals("shell", arguments).ok(),
           "shift fixture installs positionals");
    const std::string_view once[]{"shift"};
    expect(fixture.run(once).status == 0 &&
               fixture.state.argument_count() == 2 &&
               fixture.state.positional(1).value == "two",
           "shift with no operand drops one argument");
    const std::string_view zero[]{"shift", "0"};
    expect(fixture.run(zero).status == 0 &&
               fixture.state.argument_count() == 2,
           "shift 0 succeeds without change");
    const std::string_view too_many[]{"shift", "9"};
    expect(fixture.run(too_many).status == 1 &&
               fixture.state.argument_count() == 2,
           "a count above $# fails and changes nothing");
    const std::string_view invalid[]{"shift", "-1"};
    expect(fixture.run(invalid).status == 2,
           "a negative count is a usage error");
}

void flow_producing_builtins() {
    Fixture fixture;
    const std::string_view leave[]{"break"};
    expect(fixture.run(leave).flow == Flow::Break,
           "break produces its evaluator flow");
    const std::string_view again[]{"continue"};
    expect(fixture.run(again).flow == Flow::Continue,
           "continue produces its evaluator flow");
    const std::string_view counted[]{"break", "2"};
    const auto refused = fixture.run(counted);
    expect(refused.flow == Flow::Normal && refused.status == 2 &&
               refused.error == Status::Unsupported,
           "a loop count operand is not part of the embedded profile");

    const std::string_view pause[]{"yield"};
    expect(fixture.run(pause).flow == Flow::Yield &&
               fixture.run(pause).status == 0,
           "yield returns to the application");

    fixture.state.last_status = 3;
    const std::string_view bare[]{"return"};
    auto result = fixture.run(bare);
    expect(result.flow == Flow::Return && result.status == 3,
           "return with no operand carries the last status");
    const std::string_view coded[]{"exit", "42"};
    result = fixture.run(coded);
    expect(result.flow == Flow::Exit && result.status == 42,
           "exit carries its operand");
    const std::string_view wide[]{"exit", "256"};
    result = fixture.run(wide);
    expect(result.flow == Flow::Normal && result.status == 2,
           "a status outside 0 through 255 is a usage error");
}

void introspection_builtins() {
    Fixture fixture;
    const std::string_view present[]{"capability", "variables"};
    expect(fixture.run(present).status == 0,
           "a level-1 capability is present");
    const std::string_view absent[]{"capability", "gpio"};
    expect(fixture.run(absent).status == 1,
           "an unserved capability reports status 1");
    const std::string_view unknown[]{"capability", "warp"};
    expect(fixture.run(unknown).status == 2,
           "an unknown capability name is a usage error");
    const std::string_view listed[]{"capability"};
    expect(fixture.run(listed).status == 0 &&
               fixture.written().find("variables\n") !=
                   std::string_view::npos &&
               fixture.written().find("gpio") == std::string_view::npos,
           "capability lists exactly the served names");

    Fixture helped;
    expect(helped.registry.install({
               .name = "sample",
               .summary = "installed sample",
               .command_class = mm::shell::CommandClass::Custom,
               .required_capabilities = {},
               .handler = helped.find("true")->handler,
               .context = nullptr,
           }).ok(),
           "help fixture installs a visible command");
    const std::string_view named[]{"help", "sample"};
    expect(helped.run(named).status == 0 &&
               helped.written() == "sample installed sample\n",
           "help describes one installed command");
    const std::string_view unlisted[]{"help", "absent"};
    const auto missing = helped.run(unlisted);
    expect(missing.status == 1 && missing.error == Status::NotFound,
           "help reports an unknown name");

    Fixture scripted;
    expect(scripted.scripts.install({
               .name = "blink",
               .summary = "installed blink",
               .required_capabilities = {},
               .source = mm::shell::SourceView{"true"},
           }, &scripted.registry).ok(),
           "help fixture installs a script");
    const std::string_view scripted_name[]{"help", "blink"};
    expect(scripted.run(scripted_name).status == 0 &&
               scripted.written() == "blink installed blink\n",
           "help describes an installed script");

    Fixture whole;
    expect(whole.scripts.install({
               .name = "blink",
               .summary = "installed blink",
               .required_capabilities = {},
               .source = mm::shell::SourceView{"true"},
           }, &whole.registry).ok(),
           "listing fixture installs a script");
    const std::string_view everything[]{"help"};
    expect(whole.run(everything).status == 0 &&
               whole.written() == "blink installed blink\n",
           "help lists installed scripts after native commands");

    Fixture resolved;
    expect(resolved.registry.install({
               .name = "sample",
               .summary = "installed sample",
               .command_class = mm::shell::CommandClass::Custom,
               .required_capabilities = {},
               .handler = resolved.find("false")->handler,
               .context = nullptr,
           }).ok(),
           "command fixture installs a visible command");
    const std::string_view verbose[]{"command", "-v", "sample"};
    expect(resolved.run(verbose).status == 0 &&
               resolved.written() == "sample\n",
           "command -v names a resolved command");
    const std::string_view invoked[]{"command", "sample"};
    expect(resolved.run(invoked).status == 1,
           "command runs the resolved command");
    const std::string_view nowhere[]{"command", "-v", "absent"};
    const auto not_found = resolved.run(nowhere);
    expect(not_found.status == 127 && not_found.error == Status::NotFound,
           "command reports an unresolved name with status 127");
}

void installs_the_level1_pack_atomically() {
    Fixture fixture;
    // Room for two packs, so a second install reaches the duplicate check
    // rather than stopping at the capacity check that precedes it.
    mm::shell::CommandDescriptor slots[mm::shell::core_builtin_count * 2 +
                                       2]{};
    Registry registry{slots};
    mm::shell::Introspection binding{nullptr, &fixture.scripts};
    expect(mm::shell::install_level1(registry, binding).ok() &&
               registry.count() == mm::shell::core_builtin_count &&
               binding.registry == &registry,
           "the level-1 pack installs and binds its own registry");
    expect(registry.find(":") != nullptr &&
               registry.find(":")->command_class ==
                   mm::shell::CommandClass::SpecialBuiltin &&
               registry.find("[") != nullptr &&
               registry.find("run") != nullptr,
           "the pack carries the special builtins the public entry refuses");
    expect(registry.install(*registry.find(":")).status ==
               Status::BadArgument,
           "the public entry still refuses a special builtin");

    const auto again = mm::shell::install_level1(registry, binding);
    expect(again.status == Status::Duplicate &&
               registry.count() == mm::shell::core_builtin_count,
           "a second install is a duplicate and changes nothing");

    expect(registry.install({
               .name = "echo",
               .summary = "custom echo",
               .command_class = mm::shell::CommandClass::Custom,
               .required_capabilities = {},
               .handler = fixture.find("true")->handler,
               .context = nullptr,
           }).status == Status::Duplicate,
           "a custom command cannot take a name the pack owns");
    expect(registry.install({
               .name = "device",
               .summary = "custom device",
               .command_class = mm::shell::CommandClass::Custom,
               .required_capabilities = {},
               .handler = fixture.find("true")->handler,
               .context = nullptr,
           }).ok() &&
               registry.count() == mm::shell::core_builtin_count + 1,
           "a custom command installs beside the pack");

    mm::shell::CommandDescriptor narrow[mm::shell::core_builtin_count - 1]{};
    Registry small{narrow};
    mm::shell::Introspection unused{nullptr, nullptr};
    const auto refused = mm::shell::install_level1(small, unused);
    expect(refused.status == Status::Overflow &&
               refused.overflow.storage_class ==
                   mm::shell::StorageClass::CustomCommands &&
               small.count() == 0,
           "a short registry span installs nothing");
}

const mm::test::case_ cases[]{
    {"core pack", &publishes_the_core_pack},
    {"trivial status commands", &trivial_status_commands},
    {"echo and printf", &echo_and_printf_write_bytes},
    {"full sink", &full_sink_fails_without_partial_retry},
    {"embedded test operators", &embedded_test_operators},
    {"set and shift", &set_and_shift_are_transactional},
    {"flow producing builtins", &flow_producing_builtins},
    {"introspection builtins", &introspection_builtins},
    {"level-1 pack installs atomically",
     &installs_the_level1_pack_atomically},
};

const mm::test::registrar reg{"mm.shell core builtins", cases};

}  // namespace
