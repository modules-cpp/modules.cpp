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
using mm::shell::Evaluator;
using mm::shell::EvaluatorStorage;
using mm::shell::Flow;
using mm::shell::Registry;
using mm::shell::SourceView;
using mm::shell::Status;
using mm::shell::Step;
using mm::shell::StepResult;
using mm::test::expect;

struct ScriptScratch {
    mm::shell::ScriptToken tokens[96]{};
    mm::shell::WordFragment fragments[96]{};
    mm::shell::SyntaxNode nodes[96]{};
    mm::shell::SyntaxLink links[96]{};
    mm::shell::ParserFrame context[32]{};

    [[nodiscard]] mm::shell::ScriptStorage storage() {
        return {tokens, fragments, nodes, links, context};
    }
};

struct RuntimeScratch {
    mm::shell::FieldPiece pieces[64]{};
    char generated[128]{};
    char field_text[128]{};
    mm::shell::SourceSpan fields[32]{};
    mm::shell::VariableSlot shadow_variables[8]{};
    char shadow_text[128]{};
    std::string_view arguments[32]{};
    char argument_text[256]{};
    mm::shell::EvaluatorFrame frames[16]{};
    std::string_view loop_items[16]{};
    char loop_text[128]{};
    mm::shell::PatternByte pattern[64]{};
    mm::shell::VariableSlot prefix_variables[16]{};
    char prefix_variable_text[256]{};
    char staged_output[64]{};
    char staged_error[64]{};
    mm::shell::PositionalSlot call_positionals[32]{};
    char capture_text[128]{};
    std::string_view capture_values[8]{};
    mm::shell::VariableSlot capture_variables[16]{};
    char capture_variable_text[256]{};
    mm::shell::ScriptToken capture_tokens[48]{};
    mm::shell::WordFragment capture_fragments[48]{};
    mm::shell::SyntaxNode capture_nodes[48]{};
    mm::shell::SyntaxLink capture_links[48]{};
    mm::shell::ParserFrame capture_parser_context[16]{};

    [[nodiscard]] EvaluatorStorage storage() {
        return {{pieces, generated, {field_text, fields},
                 shadow_variables, shadow_text},
                arguments, argument_text, frames, loop_items, loop_text,
                pattern, prefix_variables, prefix_variable_text,
                staged_output, staged_error, nullptr, nullptr,
                call_positionals};
    }
};

struct FunctionScratch {
    mm::shell::FunctionSlot functions[4]{};
    char text[512]{};
    mm::shell::ScriptToken tokens[64]{};
    mm::shell::WordFragment fragments[64]{};
    mm::shell::SyntaxNode nodes[64]{};
    mm::shell::SyntaxLink links[64]{};
    mm::shell::ParserFrame context[16]{};

    [[nodiscard]] mm::shell::FunctionStorage storage() {
        return {functions, text, tokens, fragments, nodes, links, context};
    }
};

struct ScriptScratchArena {
    mm::shell::ScriptSlot slots[3]{};
    mm::shell::ScriptToken tokens[64]{};
    mm::shell::WordFragment fragments[64]{};
    mm::shell::SyntaxNode nodes[64]{};
    mm::shell::SyntaxLink links[64]{};
    mm::shell::ParserFrame context[16]{};

    [[nodiscard]] mm::shell::ScriptLibraryStorage storage() {
        return {slots, tokens, fragments, nodes, links, context};
    }
};

struct HandlerState {
    std::size_t calls = 0;
    std::string_view last_arg;
    int result = 0;
};

void command_handler(void* data,
                     std::span<const std::string_view> args,
                     CommandContext&, CommandResult& result) {
    auto& state = *static_cast<HandlerState*>(data);
    ++state.calls;
    state.last_arg = args.size() > 1 ? args[1] : std::string_view{};
    result.status = state.result;
    if (args.size() > 1 && args[1] == "fail") result.status = 1;
}

struct CounterState {
    std::size_t remaining = 0;
    std::size_t calls = 0;
};

void counter_handler(void* data, std::span<const std::string_view>,
                     CommandContext&, CommandResult& result) {
    auto& state = *static_cast<CounterState*>(data);
    ++state.calls;
    if (state.remaining == 0) {
        result.status = 1;
        return;
    }
    --state.remaining;
    result.status = 0;
}

void flow_handler(void*, std::span<const std::string_view> args,
                  CommandContext&, CommandResult& result) {
    const auto which = args.size() > 1 ? args[1] : std::string_view{};
    if (which == "break") result.flow = Flow::Break;
    if (which == "continue") result.flow = Flow::Continue;
    if (which == "return") {
        result.flow = Flow::Return;
        if (args.size() > 2 && args[2] == "3") result.status = 3;
    }
    if (which == "yield") result.flow = Flow::Yield;
    if (which == "exit") {
        result.flow = Flow::Exit;
        result.status = 7;
    }
}

void writer_handler(void*, std::span<const std::string_view> args,
                    CommandContext& context, CommandResult& result) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (context.io.out.write(args[i]) !=
            mm::shell::SinkResult::Accepted) {
            const auto refused = context.io.out.failure();
            result.status = 1;
            result.error = refused.error;
            result.overflow = refused.overflow;
            return;
        }
    }
}

// A transport that refuses a fixed number of writes before accepting, the
// shape an application's pump has when it has no room yet.
struct BlockingSink {
    char buffer[128]{};
    std::size_t written = 0;
    std::size_t blocks = 0;
    std::size_t refusals = 0;

    [[nodiscard]] mm::shell::ByteSink sink() {
        return {
            .context = this,
            .write_fn = &BlockingSink::write_callback,
            .flush_fn = nullptr,
            .failure_fn = &BlockingSink::failure_callback,
        };
    }

    [[nodiscard]] std::string_view view() const {
        return std::string_view{buffer, written};
    }

    static mm::shell::SinkResult write_callback(
        void* ctx, std::span<const char> bytes) {
        auto* self = static_cast<BlockingSink*>(ctx);
        if (self->blocks != 0) {
            --self->blocks;
            ++self->refusals;
            return mm::shell::SinkResult::WouldBlock;
        }
        if (bytes.size() > sizeof(self->buffer) - self->written) {
            return mm::shell::SinkResult::Failed;
        }
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            self->buffer[self->written + i] = bytes[i];
        }
        self->written += bytes.size();
        return mm::shell::SinkResult::Accepted;
    }

    static mm::shell::SinkFailure failure_callback(void*) {
        return {.error = Status::WriteError};
    }
};

// A fake transaction: it preflights its whole request, response, and working
// space before touching the device, so an undersized scratch span produces
// Overflow with no output and no effect.
struct DeviceState {
    std::size_t required = 8;
    std::size_t effects = 0;
    std::size_t entries = 0;
};

void device_handler(void* data, std::span<const std::string_view> args,
                    CommandContext& context, CommandResult& result) {
    auto& state = *static_cast<DeviceState*>(data);
    ++state.entries;
    if (context.scratch.size() < state.required) {
        result.status = 2;
        result.error = Status::Overflow;
        result.overflow = {mm::shell::StorageClass::TransactionScratch,
                           state.required};
        return;
    }
    for (std::size_t i = 0; i < state.required; ++i) {
        context.scratch[i] = static_cast<std::byte>(i);
    }
    ++state.effects;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (context.io.out.write(args[i]) !=
            mm::shell::SinkResult::Accepted) {
            result.status = 1;
            result.error = Status::WriteError;
            return;
        }
    }
}

struct Fixture {
    ScriptScratch script_storage;
    RuntimeScratch runtime_storage;
    mm::shell::EmbeddedScript script;
    mm::shell::VariableSlot variables[16]{};
    char variable_text[256]{};
    mm::shell::PositionalSlot positionals[16]{};
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
    std::byte handler_scratch[8]{};
    CommandContext context{io, state, capabilities, handler_scratch};
    CommandDescriptor commands[8]{};
    Registry registry{commands};
    HandlerState handler_state;
    CounterState counter_state;
    DeviceState device_state;
    FunctionScratch function_storage;
    mm::shell::FunctionLibrary functions{function_storage.storage()};
    ScriptScratchArena script_arena;
    mm::shell::ScriptLibrary scripts{script_arena.storage()};
    FunctionScratch capture_function_storage;
    mm::shell::FunctionLibrary capture_functions{
        capture_function_storage.storage()};
    mm::shell::Introspection binding{&registry, &scripts};
    CommandDescriptor core[mm::shell::core_builtin_count]{};
    std::size_t core_count = 0;
    Evaluator evaluator;

    [[nodiscard]] EvaluatorStorage storage() {
        auto supplied = runtime_storage.storage();
        supplied.functions = &functions;
        supplied.scripts = &scripts;
        supplied.capture_text = runtime_storage.capture_text;
        supplied.capture_values = runtime_storage.capture_values;
        supplied.capture_variables = runtime_storage.capture_variables;
        supplied.capture_variable_text =
            runtime_storage.capture_variable_text;
        supplied.capture_tokens = runtime_storage.capture_tokens;
        supplied.capture_fragments = runtime_storage.capture_fragments;
        supplied.capture_nodes = runtime_storage.capture_nodes;
        supplied.capture_links = runtime_storage.capture_links;
        supplied.capture_parser_context =
            runtime_storage.capture_parser_context;
        supplied.capture_functions = &capture_functions;
        return supplied;
    }

    void install() {
        expect(registry.install({
                   .name = "ok",
                   .summary = "test command",
                   .command_class = mm::shell::CommandClass::Custom,
                   .required_capabilities = {},
                   .handler = &command_handler,
                   .context = &handler_state,
               }).ok(),
               "fixture command installs");
        expect(registry.install({
                   .name = "count",
                   .summary = "bounded loop condition",
                   .command_class = mm::shell::CommandClass::Custom,
                   .required_capabilities = {},
                   .handler = &counter_handler,
                   .context = &counter_state,
               }).ok(),
               "fixture counter installs");
        expect(registry.install({
                   .name = "flow",
                   .summary = "control flow producer",
                   .command_class = mm::shell::CommandClass::Custom,
                   .required_capabilities = {},
                   .handler = &flow_handler,
                   .context = nullptr,
               }).ok(),
               "fixture flow command installs");
        expect(registry.install({
                   .name = "write",
                   .summary = "stage bytes through the evaluator",
                   .command_class = mm::shell::CommandClass::Custom,
                   .required_capabilities = {},
                   .handler = &writer_handler,
                   .context = nullptr,
               }).ok(),
               "fixture writer installs");
        expect(registry.install({
                   .name = "device",
                   .summary = "fake bounded transaction",
                   .command_class = mm::shell::CommandClass::Custom,
                   .required_capabilities = {},
                   .handler = &device_handler,
                   .context = &device_state,
               }).ok(),
               "fixture device installs");
        // run is a core Builtin, so the registry admits it. The evaluator
        // resolves it itself, which is what these fixtures exercise.
        expect(mm::shell::core_builtins(binding, core, core_count).ok(),
               "core pack materializes");
        for (std::size_t i = 0; i < core_count; ++i) {
            if (core[i].name != "run" && core[i].name != "echo") continue;
            expect(registry.install(core[i]).ok(),
                   "fixture core descriptor installs");
        }
    }

    void parse(std::string_view source) {
        const auto parsed = mm::shell::parse_embedded(
            SourceView{source}, script_storage.storage(), script);
        expect(parsed.status == mm::shell::ParseStatus::Complete,
               "evaluator fixture parses");
    }

    void start() {
        expect(evaluator.begin(script, registry, context, storage()) ==
                   Status::Ok,
               "evaluator begins");
    }

    explicit Fixture(std::string_view source) {
        install();
        parse(source);
        start();
    }
};

[[nodiscard]] StepResult run(Fixture& fixture,
                             std::size_t limit = 4096) {
    StepResult result{};
    for (std::size_t i = 0; i < limit; ++i) {
        result = fixture.evaluator.step();
        if (result.step != Step::Running) return result;
    }
    return result;
}

void runs_sequential_and_or_commands() {
    Fixture fixture{"ok first; ok fail && ok skipped; "
                    "ok fail || ok last"};
    const auto no_work = fixture.evaluator.step(0);
    expect(no_work.step == Step::Running &&
               fixture.handler_state.calls == 0,
           "zero operation budget makes no progress");
    auto result = fixture.evaluator.step();
    while (result.step == Step::Running &&
           fixture.handler_state.calls == 0) {
        result = fixture.evaluator.step();
    }
    expect(fixture.handler_state.calls == 1 &&
               fixture.handler_state.last_arg == "first",
           "first simple command dispatches");
    result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.handler_state.calls == 4 &&
               fixture.handler_state.last_arg == "last" &&
               fixture.state.last_status == 0,
           "sequence and short-circuit lists preserve status");
}

void expands_arguments_before_dispatch() {
    Fixture fixture{"ok \"$VALUE\""};
    expect(fixture.state.assign("VALUE", "two words").ok(),
           "expansion fixture variable installs");
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.handler_state.calls == 1 &&
               fixture.handler_state.last_arg == "two words",
           "quoted expansion remains one command argument");
}

void reports_unknown_and_unsupported() {
    Fixture missing{"absent"};
    auto result = run(missing);
    expect(result.step == Step::Complete &&
               missing.state.last_status == 127 &&
               missing.handler_state.calls == 0,
           "unknown command uses shell not-found status");

    Fixture definition{"greet() { ok hi; }; greet"};
    auto without_library = definition.storage();
    without_library.functions = nullptr;
    expect(definition.evaluator.begin(definition.script,
                                      definition.registry,
                                      definition.context,
                                      without_library) == Status::Ok,
           "the library-less evaluator begins");
    result = run(definition);
    expect(result.step == Step::Complete &&
               definition.state.last_status ==
                   static_cast<int>(mm::shell::CommandStatus::NotFound) &&
               definition.handler_state.calls == 0,
           "without a function library a definition is unsupported");
}

void assignments_update_state() {
    Fixture fixture{"VALUE=first; ok $VALUE; VALUE=$VALUE-2; ok $VALUE"};
    const auto result = run(fixture);
    const auto lookup = fixture.state.lookup("VALUE");
    expect(result.step == Step::Complete &&
               fixture.state.last_status == 0 &&
               fixture.handler_state.calls == 2 &&
               fixture.handler_state.last_arg == "first-2" &&
               lookup.found && lookup.value == "first-2",
           "assignment-only commands publish unsplit values");
}

void prefix_assignments_do_not_persist() {
    Fixture fixture{"VALUE=outer; VALUE=inner ok $VALUE; ok $VALUE"};
    const auto result = run(fixture);
    const auto lookup = fixture.state.lookup("VALUE");
    expect(result.step == Step::Complete &&
               fixture.handler_state.calls == 2 &&
               fixture.handler_state.last_arg == "outer" &&
               lookup.found && lookup.value == "outer",
           "a prefix assignment on a custom command is reverted");
}

void runs_if_branches() {
    Fixture taken{"if ok yes; then ok then; else ok else; fi"};
    auto result = run(taken);
    expect(result.step == Step::Complete &&
               taken.handler_state.calls == 2 &&
               taken.handler_state.last_arg == "then",
           "a true condition runs the then branch");

    Fixture alternative{
        "if ok fail; then ok then; elif ok fail; then ok elif; "
        "else ok else; fi"};
    result = run(alternative);
    expect(result.step == Step::Complete &&
               alternative.handler_state.calls == 3 &&
               alternative.handler_state.last_arg == "else",
           "failing conditions fall through elif to else");

    Fixture empty{"if ok fail; then ok then; fi"};
    result = run(empty);
    expect(result.step == Step::Complete &&
               empty.state.last_status == 0 &&
               empty.handler_state.calls == 1,
           "an if with no taken branch succeeds");
}

void runs_while_loops() {
    Fixture fixture{"while count; do ok body; done"};
    fixture.counter_state.remaining = 3;
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.counter_state.calls == 4 &&
               fixture.handler_state.calls == 3 &&
               fixture.state.last_status == 0,
           "while repeats its body until the condition fails");
}

void runs_for_loops() {
    Fixture words{"for item in one two three; do ok $item; done"};
    auto result = run(words);
    expect(result.step == Step::Complete &&
               words.handler_state.calls == 3 &&
               words.handler_state.last_arg == "three" &&
               words.state.lookup("item").value == "three",
           "for iterates an explicit word list");

    Fixture positional{"for item; do ok $item; done"};
    const std::string_view arguments[]{"alpha", "beta"};
    expect(positional.state.set_positionals("shell", arguments).ok(),
           "positional fixture installs");
    positional.start();
    result = run(positional);
    expect(result.step == Step::Complete &&
               positional.handler_state.calls == 2 &&
               positional.handler_state.last_arg == "beta",
           "for without in iterates the positional parameters");

    Fixture none{"for item in $MISSING; do ok $item; done"};
    result = run(none);
    expect(result.step == Step::Complete &&
               none.handler_state.calls == 0 &&
               none.state.last_status == 0,
           "an empty word list runs no iteration and succeeds");
}

void matches_case_patterns() {
    Fixture fixture{
        "VALUE=beta; case $VALUE in alpha) ok one;; b*|c*) ok two;; "
        "*) ok other;; esac"};
    auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.handler_state.calls == 1 &&
               fixture.handler_state.last_arg == "two",
           "an alternative pattern selects its case body");

    Fixture quoted{"VALUE=\"*\"; case $VALUE in \"*\") ok literal;; "
                   "?) ok single;; esac"};
    result = run(quoted);
    expect(result.step == Step::Complete &&
               quoted.handler_state.calls == 1 &&
               quoted.handler_state.last_arg == "literal",
           "a quoted metacharacter matches literally");

    Fixture unmatched{"VALUE=zulu; case $VALUE in alpha) ok one;; esac"};
    result = run(unmatched);
    expect(result.step == Step::Complete &&
               unmatched.handler_state.calls == 0 &&
               unmatched.state.last_status == 0,
           "an unmatched case succeeds without running a body");
}

void brace_groups_and_negation() {
    Fixture group{"{ ok one; ok two; }"};
    auto result = run(group);
    expect(result.step == Step::Complete &&
               group.handler_state.calls == 2 &&
               group.handler_state.last_arg == "two",
           "a brace group runs its list in order");

    Fixture negated{"! { ok fail; }"};
    result = run(negated);
    expect(result.step == Step::Complete &&
               negated.state.last_status == 0,
           "negation inverts a compound command status");

    Fixture simple{"! ok done"};
    result = run(simple);
    expect(result.step == Step::Complete &&
               simple.state.last_status == 1,
           "negation inverts a simple command status");
}

void control_flow_finds_its_owner() {
    Fixture breaking{
        "for item in one two three; do ok $item; flow break; done; ok after"};
    auto result = run(breaking);
    expect(result.step == Step::Complete &&
               breaking.handler_state.calls == 2 &&
               breaking.handler_state.last_arg == "after",
           "break leaves the innermost loop");

    Fixture continuing{
        "while count; do flow continue; ok body; done"};
    continuing.counter_state.remaining = 2;
    result = run(continuing);
    expect(result.step == Step::Complete &&
               continuing.handler_state.calls == 0 &&
               continuing.counter_state.calls == 3,
           "continue restarts the loop without finishing the body");

    Fixture stray{"flow break; ok after"};
    result = run(stray);
    expect(result.step == Step::Complete &&
               stray.handler_state.calls == 1,
           "break outside a loop is a usage error, not a crash");

    Fixture returning{"flow return"};
    result = run(returning);
    expect(result.step == Step::Complete &&
               returning.state.last_status == 2,
           "return without a call frame reports status 2");
}

void yield_and_exit_leave_the_evaluator() {
    Fixture yielding{"ok one; flow yield; ok two"};
    auto result = run(yielding);
    expect(result.step == Step::Yielded &&
               yielding.handler_state.calls == 1,
           "yield returns to the application between commands");
    result = run(yielding);
    expect(result.step == Step::Complete &&
               yielding.handler_state.calls == 2,
           "a yielded evaluator resumes after the yield");

    Fixture exiting{"for item in one two; do flow exit; done; ok never"};
    result = run(exiting);
    expect(result.step == Step::Complete &&
               result.command.status == 7 &&
               exiting.handler_state.calls == 0,
           "exit abandons every open frame");
}

void bounded_frames_and_budget() {
    Fixture nested{"if ok a; then while count; do for i in one; do "
                   "{ ok deep; }; done; done; fi"};
    nested.counter_state.remaining = 1;
    const auto result = run(nested);
    expect(result.step == Step::Complete &&
               nested.handler_state.calls == 2 &&
               nested.evaluator.frame_depth() == 0,
           "nested compound commands unwind every frame");

    Fixture budgeted{"if ok a; then ok b; fi"};
    const auto first = budgeted.evaluator.step(1);
    expect(first.step == Step::Running &&
               budgeted.handler_state.calls == 0,
           "one work unit performs structural work without a handler");
}

void stages_handler_output() {
    Fixture fixture{"write one; write two"};
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "onetwo",
           "staged bytes reach the application's sink in command order");

    // A staging span smaller than the handler's output keeps the accepted
    // prefix, which still drains, and reports the overflow to the caller.
    Fixture narrow{"write aaaaaaaaaa"};
    auto storage = narrow.storage();
    storage.staged_output = std::span<char>{
        narrow.runtime_storage.staged_output, 4};
    expect(narrow.evaluator.begin(narrow.script, narrow.registry,
                                  narrow.context, storage) == Status::Ok,
           "the narrow-staging evaluator begins");
    const auto refused = run(narrow);
    expect(refused.step == Step::Complete &&
               narrow.state.last_status == 1 &&
               narrow.output.view().empty(),
           "a staging overflow fails the command without partial output");
}

void one_handler_per_step() {
    Fixture fixture{
        "for item in a b c d e f g h; do ok $item && ok $item; done; "
        "if ok x; then ok y; else ok z; fi"};
    auto steps = std::size_t{0};
    StepResult result{};
    do {
        const auto before = fixture.handler_state.calls;
        result = fixture.evaluator.step();
        expect(fixture.handler_state.calls - before <= 1,
               "one step invokes at most one native handler");
        ++steps;
    } while (result.step == Step::Running && steps < 4096);
    expect(result.step == Step::Complete &&
               fixture.handler_state.calls == 18 &&
               fixture.evaluator.frame_depth() == 0,
           "a maximum-capacity script completes one handler at a time");
}

void long_scans_resume_from_a_cursor() {
    Fixture fixture{"for item in aaaa bbbb cccc dddd; do ok $item; done"};
    // A one-unit budget still materializes the whole word list before the
    // first body command, which only a retained cursor can do.
    auto steps = std::size_t{0};
    while (fixture.handler_state.calls == 0 && steps < 512) {
        const auto result = fixture.evaluator.step(1);
        expect(result.step == Step::Running,
               "a one-unit step keeps the evaluator running");
        ++steps;
    }
    expect(steps > 4 && fixture.handler_state.last_arg == "aaaa",
           "the word list is built across several bounded steps");
    StepResult result{};
    do {
        result = fixture.evaluator.step(1);
        ++steps;
    } while (result.step == Step::Running && steps < 4096);
    expect(result.step == Step::Complete &&
               fixture.handler_state.calls == 4,
           "the loop finishes on one-unit steps");
}

void bounded_transaction_scratch() {
    Fixture sufficient{"device one two"};
    auto result = run(sufficient);
    expect(result.step == Step::Complete &&
               sufficient.device_state.entries == 1 &&
               sufficient.device_state.effects == 1 &&
               sufficient.output.view() == "onetwo",
           "a sufficient scratch span performs the transaction");

    Fixture narrow{"device one"};
    narrow.device_state.required = 64;
    StepResult dispatched{};
    auto guard = std::size_t{0};
    while (narrow.device_state.entries == 0 && guard < 512) {
        dispatched = narrow.evaluator.step();
        ++guard;
    }
    expect(dispatched.command.error == Status::Overflow &&
               dispatched.command.overflow.storage_class ==
                   mm::shell::StorageClass::TransactionScratch &&
               dispatched.command.overflow.required == 64 &&
               narrow.device_state.effects == 0 &&
               narrow.output.view().empty(),
           "an undersized scratch span reports Overflow with no effect");
}

void side_effects_are_not_repeated_on_retry() {
    Fixture fixture{"device one"};
    BlockingSink transport;
    transport.blocks = 3;
    mm::shell::IoServices io{transport.sink(), fixture.error.sink()};
    CommandContext context{io, fixture.state, fixture.capabilities,
                           fixture.handler_scratch};
    expect(fixture.evaluator.begin(fixture.script, fixture.registry, context,
                                   fixture.storage()) == Status::Ok,
           "the blocking-transport evaluator begins");
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.device_state.entries == 1 &&
               fixture.device_state.effects == 1 &&
               transport.refusals == 3 && transport.view() == "one",
           "a retried drain re-enters no handler and repeats no effect");
}

void operation_units_stay_within_budget() {
    Fixture fixture{
        "for item in a b c d e f g h; do ok $item && ok $item; done; "
        "if ok x; then ok y; else ok z; fi"};
    auto steps = std::size_t{0};
    auto peak = std::size_t{0};
    StepResult result{};
    do {
        result = fixture.evaluator.step();
        expect(fixture.evaluator.last_step_units() <= 32,
               "a step never exceeds the unit budget it was given");
        expect(fixture.evaluator.last_step_handlers() <= 1,
               "a step never enters two native handlers");
        if (fixture.evaluator.last_step_units() > peak) {
            peak = fixture.evaluator.last_step_units();
        }
        ++steps;
    } while (result.step == Step::Running && steps < 4096);
    expect(result.step == Step::Complete && peak != 0 &&
               fixture.handler_state.calls == 18,
           "the adversarial script completes with observed unit counts");

    Fixture idle{"ok one"};
    const auto none = idle.evaluator.step(0);
    expect(none.step == Step::Running &&
               idle.evaluator.last_step_units() == 0 &&
               idle.evaluator.last_step_handlers() == 0,
           "a zero budget consumes no unit and enters no handler");

    Fixture single{"ok one; ok two"};
    (void)single.evaluator.step(1);
    expect(single.evaluator.last_step_units() == 1,
           "a one-unit budget charges exactly one transition");
}

void command_expansion_stays_within_argv_capacity() {
    Fixture fixture{"ok aaaa bbbb cccc"};
    auto guard = std::size_t{0};
    while (fixture.handler_state.calls == 0 && guard < 512) {
        (void)fixture.evaluator.step();
        ++guard;
    }
    constexpr auto slots = sizeof(RuntimeScratch::arguments) /
                           sizeof(RuntimeScratch::arguments[0]);
    expect(fixture.evaluator.last_command_slots() == 4 &&
               fixture.evaluator.last_command_bytes() == 14 &&
               fixture.evaluator.last_command_slots() <= slots &&
               fixture.evaluator.last_command_bytes() <=
                   sizeof(RuntimeScratch::argument_text),
           "argv expansion is accounted against the caller's capacities");

    Fixture narrow{"ok a b c d"};
    auto storage = narrow.storage();
    storage.arguments = std::span<std::string_view>{
        narrow.runtime_storage.arguments, 3};
    expect(narrow.evaluator.begin(narrow.script, narrow.registry,
                                  narrow.context, storage) == Status::Ok,
           "the narrow-argv evaluator begins");
    StepResult refused{};
    guard = 0;
    while (refused.command.error == Status::Ok && guard < 512) {
        refused = narrow.evaluator.step();
        if (refused.step != Step::Running) break;
        ++guard;
    }
    expect(refused.command.error == Status::Overflow &&
               refused.command.overflow.storage_class ==
                   mm::shell::StorageClass::ExpandedFields &&
               narrow.handler_state.calls == 0,
           "argv capacity, not the unit budget, bounds one command");
}

void errexit_ends_untested_failures() {
    Fixture fixture{"ok fail; ok never"};
    fixture.state.errexit = true;
    auto result = run(fixture);
    expect(result.step == Step::Complete && result.command.status == 1 &&
               fixture.handler_state.calls == 1 &&
               fixture.evaluator.frame_depth() == 0,
           "set -e ends the evaluator at an untested failure");

    Fixture condition{
        "if ok fail; then ok then; else ok else; fi; ok after"};
    condition.state.errexit = true;
    result = run(condition);
    expect(result.step == Step::Complete &&
               condition.handler_state.calls == 3 &&
               condition.handler_state.last_arg == "after",
           "a failing condition is a tested context");

    Fixture chained{"ok fail || ok recovered; ok after"};
    chained.state.errexit = true;
    result = run(chained);
    expect(result.step == Step::Complete &&
               chained.handler_state.calls == 3,
           "a non-final operand of || is a tested context");

    Fixture inverted{"! ok done; ok after"};
    inverted.state.errexit = true;
    result = run(inverted);
    expect(result.step == Step::Complete &&
               inverted.handler_state.calls == 2,
           "an inverted command is a tested context");

    Fixture loop{"while count; do ok fail; done; ok never"};
    loop.counter_state.remaining = 3;
    loop.state.errexit = true;
    result = run(loop);
    expect(result.step == Step::Complete && result.command.status == 1 &&
               loop.counter_state.calls == 1 &&
               loop.handler_state.calls == 1 &&
               loop.evaluator.frame_depth() == 0,
           "a loop body failure is not a tested context");
}

void nounset_ends_the_evaluator() {
    Fixture fixture{"ok $MISSING; ok never"};
    fixture.state.nounset = true;
    auto result = run(fixture);
    expect(result.step == Step::Complete && result.command.status == 1 &&
               result.command.error == Status::NotFound &&
               fixture.handler_state.calls == 0 &&
               fixture.evaluator.frame_depth() == 0,
           "set -u ends the evaluator on an unset parameter");

    Fixture guarded{"ok ${MISSING:-fallback}"};
    guarded.state.nounset = true;
    result = run(guarded);
    expect(result.step == Step::Complete &&
               guarded.handler_state.calls == 1 &&
               guarded.handler_state.last_arg == "fallback",
           "a default operand is exempt from set -u");

    Fixture list{"for item in $MISSING; do ok $item; done"};
    list.state.nounset = true;
    result = run(list);
    expect(result.step == Step::Complete && result.command.status == 1 &&
               result.command.error == Status::NotFound &&
               list.handler_state.calls == 0,
           "an unset word list ends the evaluator the same way");

    Fixture permitted{"ok $MISSING done"};
    result = run(permitted);
    expect(result.step == Step::Complete &&
               permitted.handler_state.calls == 1 &&
               permitted.handler_state.last_arg == "done",
           "without set -u an unset parameter expands to nothing");
}

void defines_and_calls_functions() {
    Fixture fixture{"greet() { write $1 $2; }; greet a b"};
    auto result = run(fixture);
    expect(result.step == Step::Complete && fixture.output.view() == "ab" &&
               fixture.functions.size() == 1 &&
               fixture.evaluator.frame_depth() == 0,
           "a definition is stored and the call sees its own arguments");

    Fixture nested{"outer() { write $1; inner x; write $1; }; "
                   "inner() { write -$1-; }; outer one"};
    result = run(nested);
    expect(result.step == Step::Complete &&
               nested.output.view() == "one-x-one",
           "a nested call restores the caller's positional parameters");

    Fixture caller{"show() { write $1; }; show inner; write $1"};
    const std::string_view arguments[]{"outer"};
    expect(caller.state.set_positionals("shell", arguments).ok(),
           "caller positionals install");
    caller.start();
    result = run(caller);
    expect(result.step == Step::Complete &&
               caller.output.view() == "innerouter" &&
               caller.state.argument_count() == 1,
           "the script's own arguments survive a call");
}

void functions_outrank_installed_commands() {
    Fixture fixture{"ok() { write shadowed; }; ok ignored"};
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "shadowed" &&
               fixture.handler_state.calls == 0,
           "a function resolves ahead of an installed command");
}

void return_leaves_the_call_frame() {
    Fixture fixture{"body() { write one; flow return; write two; }; "
                    "body; write after"};
    auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "oneafter" &&
               fixture.evaluator.frame_depth() == 0,
           "return abandons the rest of the body, not the script");

    Fixture coded{"body() { flow return 3; }; body"};
    result = run(coded);
    expect(result.step == Step::Complete && coded.state.last_status == 3,
           "return carries its status out of the call");

    Fixture stray{"flow return; write after"};
    result = run(stray);
    expect(result.step == Step::Complete && stray.state.last_status == 0 &&
               stray.output.view() == "after",
           "return outside a call is a usage error that does not unwind");

    Fixture looping{"body() { for i in a b; do flow break; done; write done; }; "
                    "body"};
    result = run(looping);
    expect(result.step == Step::Complete &&
               looping.output.view() == "done",
           "break inside a body finds the loop, not the call frame");

    Fixture escaping{"body() { flow break; write inner; }; "
                     "for i in a b; do body; write x; done"};
    result = run(escaping);
    expect(result.step == Step::Complete &&
               escaping.output.view() == "innerxinnerx" &&
               escaping.state.last_status == 0,
           "a call frame stops break from reaching the caller's loop");
}

void recursion_is_bounded() {
    Fixture fixture{"deep() { deep; }; deep"};
    auto storage = fixture.storage();
    storage.call_limit = 4;
    expect(fixture.evaluator.begin(fixture.script, fixture.registry,
                                   fixture.context, storage) == Status::Ok,
           "the bounded-recursion evaluator begins");
    StepResult refused{};
    auto guard = std::size_t{0};
    while (guard < 4096) {
        refused = fixture.evaluator.step();
        if (refused.command.error == Status::Overflow) break;
        if (refused.step != Step::Running) break;
        ++guard;
    }
    expect(refused.command.error == Status::Overflow &&
               refused.command.overflow.storage_class ==
                   mm::shell::StorageClass::EvaluatorFrames &&
               refused.command.overflow.required == 5,
           "recursion past the call limit is refused, not crashed");
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.evaluator.frame_depth() == 0,
           "the refused recursion unwinds every call frame");
}

void invokes_installed_scripts() {
    Fixture fixture{"greeting a b"};
    expect(fixture.scripts.install({
               .name = "greeting",
               .summary = "installed greeting",
               .required_capabilities = {},
               .source = SourceView{"write $1 $2"},
           }, &fixture.registry).ok(),
           "the script installs");
    fixture.start();
    auto result = run(fixture);
    expect(result.step == Step::Complete && fixture.output.view() == "ab" &&
               fixture.evaluator.frame_depth() == 0,
           "a script is invoked directly with its own arguments");

    Fixture explicitly{"run greeting x y; write -$1-"};
    expect(explicitly.scripts.install({
               .name = "greeting",
               .summary = "installed greeting",
               .required_capabilities = {},
               .source = SourceView{"write $0 $1 $2"},
           }, &explicitly.registry).ok(),
           "the run fixture script installs");
    const std::string_view arguments[]{"outer"};
    expect(explicitly.state.set_positionals("shell", arguments).ok(),
           "caller positionals install");
    explicitly.start();
    result = run(explicitly);
    expect(result.step == Step::Complete &&
               explicitly.output.view() == "greetingxy-outer-",
           "run names the script as $0 and restores the caller after");
}

void script_invocation_reports_its_refusals() {
    Fixture missing{"run absent"};
    auto result = run(missing);
    expect(result.step == Step::Complete &&
               missing.state.last_status ==
                   static_cast<int>(mm::shell::CommandStatus::NotFound),
           "run reports an uninstalled name with status 127");

    Fixture bare{"run"};
    result = run(bare);
    expect(result.step == Step::Complete && bare.state.last_status == 2,
           "run with no operand is a usage error");

    Fixture gated{"blink"};
    mm::shell::CapabilitySet needs_gpio;
    needs_gpio.set(mm::shell::Capability::Gpio);
    expect(gated.scripts.install({
               .name = "blink",
               .summary = "needs gpio",
               .required_capabilities = needs_gpio,
               .source = SourceView{"write on"},
           }, &gated.registry).ok(),
           "the gated script installs without the capability");
    gated.start();
    result = run(gated);
    expect(result.step == Step::Complete &&
               gated.state.last_status ==
                   static_cast<int>(mm::shell::CommandStatus::Unavailable) &&
               gated.output.view().empty(),
           "an unserved capability is reported before the first command");
}

void functions_shadow_installed_scripts() {
    Fixture fixture{"greeting() { write function; }; greeting; "
                    "run greeting"};
    expect(fixture.scripts.install({
               .name = "greeting",
               .summary = "installed greeting",
               .required_capabilities = {},
               .source = SourceView{"write script"},
           }, &fixture.registry).ok(),
           "the shadowed script installs");
    fixture.start();
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "functionscript",
           "a function shadows a script, and run still selects the script");
}

void return_leaves_an_installed_script() {
    Fixture fixture{"early; write after"};
    expect(fixture.scripts.install({
               .name = "early",
               .summary = "returns early",
               .required_capabilities = {},
               .source = SourceView{"write one; flow return 3; write two"},
           }, &fixture.registry).ok(),
           "the early-return script installs");
    fixture.start();
    const auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "oneafter",
           "return leaves the script, not the calling program");
}

void substitutes_command_output() {
    Fixture fixture{"write -$(echo inner)-"};
    auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "-inner-" &&
               fixture.evaluator.frame_depth() == 0,
           "a substitution runs first and its trailing newline is trimmed");

    Fixture several{"write $(echo a) $(echo b)"};
    result = run(several);
    expect(result.step == Step::Complete &&
               several.output.view() == "ab",
           "several substitutions in one command resolve in order");

    Fixture assigned{"v=$(echo hi); write $v"};
    result = run(assigned);
    expect(result.step == Step::Complete &&
               assigned.output.view() == "hi" &&
               assigned.state.lookup("v").value == "hi",
           "an assignment value may come from a substitution");

    Fixture quoted{"write \"$(echo one two)\""};
    result = run(quoted);
    expect(result.step == Step::Complete &&
               quoted.output.view() == "one two",
           "a quoted substitution stays one field");

    // The application keeps control between the nested command and the
    // command that consumes its output.
    Fixture stepped{"write -$(echo inner)-"};
    auto steps = std::size_t{0};
    auto handler_steps = std::size_t{0};
    do {
        result = stepped.evaluator.step();
        expect(stepped.evaluator.last_step_handlers() <= 1,
               "a step through a substitution enters at most one handler");
        if (stepped.evaluator.last_step_handlers() != 0) ++handler_steps;
        ++steps;
    } while (result.step == Step::Running && steps < 4096);
    expect(result.step == Step::Complete && handler_steps == 2 &&
               stepped.output.view() == "-inner-",
           "the nested command and its consumer occupy separate steps");
}

void substitution_child_state_is_isolated() {
    Fixture fixture{"x=outer; write $(x=inner; write $x)$x"};
    auto result = run(fixture);
    expect(result.step == Step::Complete &&
               fixture.output.view() == "innerouter" &&
               fixture.state.lookup("x").value == "outer",
           "the child's assignments do not return to the parent");

    Fixture defined{"f() { write parent; }; write $(f() { write child; }; f); "
                    "f"};
    result = run(defined);
    expect(result.step == Step::Complete &&
               defined.output.view() == "childparent" &&
               defined.functions.size() == 1,
           "a definition inside a substitution disappears with the child");

    Fixture effects{"write $(device one)"};
    result = run(effects);
    expect(result.step == Step::Complete &&
               effects.device_state.effects == 1 &&
               effects.output.view() == "one",
           "a side effect inside a substitution is real and stays performed");
}

void substitution_refusals_publish_nothing() {
    Fixture narrow{"write -$(echo aaaaaaaaaa)-"};
    auto storage = narrow.storage();
    storage.capture_text = std::span<char>{
        narrow.runtime_storage.capture_text, 4};
    expect(narrow.evaluator.begin(narrow.script, narrow.registry,
                                  narrow.context, storage) == Status::Ok,
           "the narrow-capture evaluator begins");
    auto result = run(narrow);
    expect(result.step == Step::Failed &&
               result.command.error == Status::Overflow &&
               narrow.output.view().empty(),
           "capture overflow fails the expansion without publishing text");

    Fixture nested{"write $(write $(echo x))"};
    result = run(nested);
    expect(result.step == Step::Failed &&
               result.command.error == Status::Unsupported,
           "a substitution inside a substitution is refused at this level");

    Fixture unavailable{"write $(echo hi)"};
    auto without = unavailable.storage();
    without.capture_text = {};
    expect(unavailable.evaluator.begin(unavailable.script,
                                       unavailable.registry,
                                       unavailable.context,
                                       without) == Status::Ok,
           "the capture-less evaluator begins");
    result = run(unavailable);
    expect(result.step == Step::Failed &&
               result.command.error == Status::Unsupported,
           "without capture storage a substitution is unsupported");
}

void records_tested_contexts() {
    Fixture fixture{"ok left && ok right"};
    auto result = fixture.evaluator.step();
    while (result.step == Step::Running &&
           fixture.handler_state.calls == 0) {
        result = fixture.evaluator.step();
    }
    expect(fixture.evaluator.tested_context(),
           "the left operand of && is a tested context");
    while (result.step == Step::Running &&
           fixture.handler_state.calls == 1) {
        result = fixture.evaluator.step();
    }
    expect(!fixture.evaluator.tested_context(),
           "the last operand of && is not a tested context");
}

const mm::test::case_ cases[]{
    {"sequential and-or commands", &runs_sequential_and_or_commands},
    {"expands arguments before dispatch", &expands_arguments_before_dispatch},
    {"unknown and unsupported", &reports_unknown_and_unsupported},
    {"assignments update state", &assignments_update_state},
    {"prefix assignments do not persist",
     &prefix_assignments_do_not_persist},
    {"if branches", &runs_if_branches},
    {"while loops", &runs_while_loops},
    {"for loops", &runs_for_loops},
    {"case patterns", &matches_case_patterns},
    {"brace groups and negation", &brace_groups_and_negation},
    {"control flow ownership", &control_flow_finds_its_owner},
    {"yield and exit", &yield_and_exit_leave_the_evaluator},
    {"bounded frames and budget", &bounded_frames_and_budget},
    {"stages handler output", &stages_handler_output},
    {"one handler per step", &one_handler_per_step},
    {"long scans resume", &long_scans_resume_from_a_cursor},
    {"bounded transaction scratch", &bounded_transaction_scratch},
    {"side effects not repeated", &side_effects_are_not_repeated_on_retry},
    {"operation units within budget", &operation_units_stay_within_budget},
    {"argv capacity bounds a command",
     &command_expansion_stays_within_argv_capacity},
    {"errexit ends untested failures", &errexit_ends_untested_failures},
    {"nounset ends the evaluator", &nounset_ends_the_evaluator},
    {"defines and calls functions", &defines_and_calls_functions},
    {"functions outrank commands", &functions_outrank_installed_commands},
    {"return leaves the call frame", &return_leaves_the_call_frame},
    {"recursion is bounded", &recursion_is_bounded},
    {"invokes installed scripts", &invokes_installed_scripts},
    {"script refusals", &script_invocation_reports_its_refusals},
    {"functions shadow scripts", &functions_shadow_installed_scripts},
    {"return leaves a script", &return_leaves_an_installed_script},
    {"substitutes command output", &substitutes_command_output},
    {"substitution child state", &substitution_child_state_is_isolated},
    {"substitution refusals", &substitution_refusals_publish_nothing},
    {"tested contexts", &records_tested_contexts},
};

const mm::test::registrar reg{"mm.shell evaluator", cases};

}  // namespace
