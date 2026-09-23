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

    [[nodiscard]] EvaluatorStorage storage() {
        return {{pieces, generated, {field_text, fields},
                 shadow_variables, shadow_text},
                arguments, argument_text, frames, loop_items, loop_text,
                pattern, prefix_variables, prefix_variable_text};
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
    if (which == "return") result.flow = Flow::Return;
    if (which == "yield") result.flow = Flow::Yield;
    if (which == "exit") {
        result.flow = Flow::Exit;
        result.status = 7;
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
    mm::shell::MemorySink output{{}};
    mm::shell::MemorySink error{{}};
    mm::shell::IoServices io{output.sink(), error.sink()};
    mm::shell::CapabilitySet capabilities =
        mm::shell::CapabilitySet::level1();
    std::byte handler_scratch[8]{};
    CommandContext context{io, state, capabilities, handler_scratch};
    CommandDescriptor commands[4]{};
    Registry registry{commands};
    HandlerState handler_state;
    CounterState counter_state;
    Evaluator evaluator;

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
    }

    void parse(std::string_view source) {
        const auto parsed = mm::shell::parse_embedded(
            SourceView{source}, script_storage.storage(), script);
        expect(parsed.status == mm::shell::ParseStatus::Complete,
               "evaluator fixture parses");
    }

    void start() {
        expect(evaluator.begin(script, registry, context,
                               runtime_storage.storage()) == Status::Ok,
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

    Fixture definition{"greet() { ok hi; }"};
    result = run(definition);
    expect(result.step == Step::Failed &&
               result.command.error == Status::Unsupported &&
               definition.handler_state.calls == 0,
           "function definitions are not yet evaluated");
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
    {"tested contexts", &records_tested_contexts},
};

const mm::test::registrar reg{"mm.shell evaluator", cases};

}  // namespace
