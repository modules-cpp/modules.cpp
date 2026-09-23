// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
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
using mm::shell::Registry;
using mm::shell::SourceView;
using mm::shell::Status;
using mm::test::expect;

struct ScriptScratch {
    mm::shell::ScriptToken tokens[64]{};
    mm::shell::WordFragment fragments[64]{};
    mm::shell::SyntaxNode nodes[64]{};
    mm::shell::SyntaxLink links[64]{};
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

    [[nodiscard]] EvaluatorStorage storage() {
        return {{pieces, generated, {field_text, fields},
                 shadow_variables, shadow_text}, arguments,
                argument_text};
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

struct Fixture {
    ScriptScratch script_storage;
    RuntimeScratch runtime_storage;
    mm::shell::EmbeddedScript script;
    mm::shell::ShellState state;
    mm::shell::MemorySink output{{}};
    mm::shell::MemorySink error{{}};
    mm::shell::IoServices io{output.sink(), error.sink()};
    mm::shell::CapabilitySet capabilities =
        mm::shell::CapabilitySet::level1();
    std::byte handler_scratch[8]{};
    CommandContext context{io, state, capabilities, handler_scratch};
    CommandDescriptor commands[2]{};
    Registry registry{commands};
    HandlerState handler_state;
    Evaluator evaluator;

    explicit Fixture(std::string_view source) {
        const auto parsed = mm::shell::parse_embedded(
            SourceView{source}, script_storage.storage(), script);
        mm::test::expect(parsed.status ==
                             mm::shell::ParseStatus::Complete,
                         "evaluator fixture parses");
        const auto installed = registry.install({
            .name = "ok",
            .summary = "test command",
            .command_class = mm::shell::CommandClass::Custom,
            .required_capabilities = {},
            .handler = &command_handler,
            .context = &handler_state,
        });
        mm::test::expect(installed.ok(), "fixture command installs");
        mm::test::expect(evaluator.begin(script, registry, context,
                                         runtime_storage.storage()) ==
                             Status::Ok,
                         "evaluator begins");
    }
};

void runs_sequential_and_or_commands() {
    Fixture fixture{"ok first; ok fail && ok skipped; "
                    "ok fail || ok last"};
    const auto no_work = fixture.evaluator.step(0);
    expect(no_work.step == mm::shell::Step::Running &&
               fixture.handler_state.calls == 0,
           "zero operation budget makes no progress");
    auto result = fixture.evaluator.step();
    expect(result.step == mm::shell::Step::Running &&
               fixture.handler_state.calls == 1 &&
               fixture.handler_state.last_arg == "first",
           "first simple command dispatches");
    while (result.step == mm::shell::Step::Running) {
        result = fixture.evaluator.step();
    }
    expect(result.step == mm::shell::Step::Complete &&
               fixture.handler_state.calls == 4 &&
               fixture.handler_state.last_arg == "last" &&
               fixture.state.last_status == 0,
           "sequence and short-circuit lists preserve status");
}

void expands_arguments_before_dispatch() {
    Fixture fixture{"ok \"$VALUE\""};
    mm::shell::VariableSlot variables[2]{};
    char text[32]{};
    fixture.state = mm::shell::ShellState{variables, text, {}, {}};
    expect(fixture.state.assign("VALUE", "two words").ok(),
           "expansion fixture variable installs");
    expect(fixture.evaluator.begin(
               fixture.script, fixture.registry, fixture.context,
               fixture.runtime_storage.storage()) == Status::Ok,
           "evaluator restarts with assigned state");
    const auto command = fixture.evaluator.step();
    expect(command.step == mm::shell::Step::Running &&
               fixture.handler_state.calls == 1 &&
               fixture.handler_state.last_arg == "two words",
           "quoted expansion remains one command argument");
}

void reports_unknown_and_unsupported() {
    Fixture missing{"absent"};
    auto result = missing.evaluator.step();
    expect(result.step == mm::shell::Step::Running &&
               result.command.status ==
                   static_cast<int>(mm::shell::CommandStatus::NotFound) &&
               missing.state.last_status == 127 &&
               missing.handler_state.calls == 0,
           "unknown command uses shell not-found status");

    Fixture control{"if true; then ok; fi"};
    result = control.evaluator.step();
    expect(result.step == mm::shell::Step::Failed &&
               result.command.error == Status::Unsupported &&
               control.handler_state.calls == 0,
           "unimplemented control structures fail explicitly");
}

const mm::test::case_ cases[]{
    {"sequential and-or commands", &runs_sequential_and_or_commands},
    {"expands arguments before dispatch", &expands_arguments_before_dispatch},
    {"unknown and unsupported", &reports_unknown_and_unsupported},
};

const mm::test::registrar reg{"mm.shell evaluator", cases};

}  // namespace
