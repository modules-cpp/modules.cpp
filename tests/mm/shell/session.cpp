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
using mm::shell::ParseStatus;
using mm::shell::Registry;
using mm::shell::Session;
using mm::shell::SessionState;
using mm::shell::Status;
using mm::shell::Step;
using mm::shell::StepResult;
using mm::test::expect;

struct RecorderState {
    std::size_t calls = 0;
    std::string_view last_arg;
};

void recorder_handler(void* data,
                      std::span<const std::string_view> args,
                      CommandContext& context, CommandResult& result) {
    auto& state = *static_cast<RecorderState*>(data);
    ++state.calls;
    state.last_arg = args.size() > 1 ? args[1] : std::string_view{};
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (context.io.out.write(args[i]) != mm::shell::SinkResult::Accepted) {
            result.status = 1;
            result.error = Status::WriteError;
            return;
        }
    }
    (void)context.io.out.write(std::string_view{"\n"});
    if (args.size() > 1 && args[1] == "fail") result.status = 1;
}

// A transport that refuses a fixed number of writes before accepting, which
// is what an application's pump looks like when it has no room yet.
struct BlockingSink {
    char buffer[256]{};
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

struct Scratch {
    mm::shell::ScriptToken tokens[96]{};
    mm::shell::WordFragment fragments[96]{};
    mm::shell::SyntaxNode nodes[96]{};
    mm::shell::SyntaxLink links[96]{};
    mm::shell::ParserFrame context[32]{};
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

    [[nodiscard]] mm::shell::ScriptStorage script() {
        return {tokens, fragments, nodes, links, context};
    }

    [[nodiscard]] mm::shell::EvaluatorStorage evaluator() {
        return {{pieces, generated, {field_text, fields}, shadow_variables,
                 shadow_text},
                arguments, argument_text, frames, loop_items, loop_text,
                pattern, prefix_variables, prefix_variable_text,
                staged_output, staged_error};
    }
};

struct Fixture {
    Scratch scratch;
    char source[128]{};
    mm::shell::VariableSlot variables[16]{};
    char variable_text[256]{};
    mm::shell::PositionalSlot positionals[16]{};
    char positional_text[128]{};
    mm::shell::ShellState state{variables, variable_text, positionals,
                                positional_text};
    BlockingSink transport;
    char error_bytes[64]{};
    mm::shell::MemorySink error{error_bytes};
    mm::shell::IoServices io{transport.sink(), error.sink()};
    mm::shell::CapabilitySet capabilities =
        mm::shell::CapabilitySet::level1();
    std::byte handler_scratch[8]{};
    CommandContext context{io, state, capabilities, handler_scratch};
    CommandDescriptor commands[2]{};
    Registry registry{commands};
    RecorderState recorder;
    Session session;

    Fixture() {
        expect(registry.install({
                   .name = "say",
                   .summary = "echo through the staging sink",
                   .command_class = mm::shell::CommandClass::Custom,
                   .required_capabilities = {},
                   .handler = &recorder_handler,
                   .context = &recorder,
               }).ok(),
               "session fixture command installs");
        expect(session.begin(registry, context,
                             {source, scratch.script(),
                              scratch.evaluator()}) == Status::Ok,
               "session begins");
    }

    [[nodiscard]] mm::shell::FeedResult feed(std::string_view text) {
        return session.feed(std::span<const char>{text.data(), text.size()});
    }

    [[nodiscard]] StepResult drain(std::size_t limit = 4096) {
        StepResult result{};
        for (std::size_t i = 0; i < limit; ++i) {
            result = session.step();
            if (result.step != Step::Running) return result;
            if (!session.executing()) return result;
        }
        return result;
    }
};

void runs_one_complete_line() {
    Fixture fixture;
    const auto fed = fixture.feed("say hello\n");
    expect(fed.ok() && fed.executing() && fed.consumed == 10,
           "a complete line starts an evaluator at its line ending");
    const auto result = fixture.drain();
    expect(result.step == Step::Complete &&
               fixture.recorder.calls == 1 &&
               fixture.recorder.last_arg == "hello" &&
               fixture.transport.view() == "hello\n" &&
               fixture.session.state() == SessionState::Ready &&
               fixture.session.buffered() == 0,
           "the script runs, its output lands, and the buffer is reclaimed");
}

void accumulates_multiline_constructs() {
    Fixture fixture;
    auto fed = fixture.feed("if say yes\n");
    expect(fed.ok() && fed.state == SessionState::Continuing,
           "an open if keeps the session accumulating");
    fed = fixture.feed("then say body\n");
    expect(fed.ok() && fed.state == SessionState::Continuing,
           "a then clause is still incomplete");
    fed = fixture.feed("fi\n");
    expect(fed.ok() && fed.executing(),
           "fi completes the construct and starts it");
    const auto result = fixture.drain();
    expect(result.step == Step::Complete &&
               fixture.recorder.calls == 2 &&
               fixture.transport.view() == "yes\nbody\n",
           "the whole multiline command executes once");
}

void empty_lines_and_editing() {
    Fixture fixture;
    auto fed = fixture.feed("\n\r\n");
    expect(fed.ok() && fed.state == SessionState::Ready &&
               fixture.session.buffered() == 0,
           "empty physical lines are no-ops");
    fed = fixture.feed("say helloX\b\b");
    expect(fed.ok() && fixture.session.buffered() == 8,
           "backspace removes buffered bytes");
    fed = fixture.feed("\x7f\x7f\x7f\x7f\x7f");
    expect(fed.ok() && fixture.session.buffered() == 3,
           "delete removes buffered bytes");
    fed = fixture.feed("\x01\x1b y\n");
    expect(fed.ok() && fed.executing(),
           "unrecognized control bytes are ignored");
    const auto result = fixture.drain();
    expect(result.step == Step::Complete &&
               fixture.recorder.last_arg == "y" &&
               fixture.recorder.calls == 1,
           "the edited line is what executes");
}

void malformed_input_is_discarded() {
    Fixture fixture;
    const auto fed = fixture.feed("say one ;; say two\n");
    expect(!fed.ok() && fed.status == Status::BadArgument &&
               fed.parse_status == ParseStatus::Malformed &&
               fixture.session.state() == SessionState::Ready &&
               fixture.session.buffered() == 0 &&
               fixture.recorder.calls == 0,
           "a malformed line is reported and discarded unexecuted");
    const auto after = fixture.feed("say three\n");
    expect(after.ok() && after.executing(),
           "the session accepts the next command");
    expect(fixture.drain().step == Step::Complete &&
               fixture.recorder.calls == 1,
           "recovery executes only the good command");
}

void overlong_input_is_reported_once() {
    Fixture fixture;
    char long_line[160]{};
    for (auto& byte : long_line) byte = 'x';
    const auto fed = fixture.feed(
        std::string_view{long_line, sizeof(long_line)});
    expect(!fed.ok() && fed.status == Status::Overflow &&
               fed.overflow.storage_class ==
                   mm::shell::StorageClass::SourceBytes &&
               fixture.session.state() == SessionState::Discarding,
           "an overlong command reports overflow and starts discarding");
    const auto more = fixture.feed("yyyy");
    expect(more.ok() && more.state == SessionState::Discarding,
           "the rest of the overlong line is swallowed silently");
    const auto ended = fixture.feed("\nsay after\n");
    expect(ended.ok() && ended.executing() &&
               fixture.drain().step == Step::Complete &&
               fixture.recorder.calls == 1 &&
               fixture.recorder.last_arg == "after",
           "the next line ending resumes the session");
}

void feeding_is_refused_while_executing() {
    Fixture fixture;
    expect(fixture.feed("say one\n").executing(), "a script is running");
    const auto refused = fixture.feed("say two\n");
    expect(refused.status == Status::Unavailable &&
               refused.state == SessionState::Executing,
           "feed is refused until the running script finishes");
    expect(fixture.drain().step == Step::Complete &&
               fixture.recorder.calls == 1,
           "only the started script ran");
}

void staged_output_drains_before_the_next_command() {
    Fixture fixture;
    fixture.transport.blocks = 3;
    expect(fixture.feed("say one; say two\n").executing(),
           "two commands are queued");
    auto result = fixture.session.step();
    // The first command ran, but its bytes are still staged.
    expect(result.step == Step::Running && fixture.recorder.calls == 1 &&
               fixture.transport.view().empty(),
           "a blocked transport leaves the first command's output staged");
    auto guard = 0;
    while (fixture.session.executing() && fixture.recorder.calls == 1 &&
           guard < 64) {
        result = fixture.session.step();
        ++guard;
    }
    expect(fixture.transport.refusals == 3 &&
               fixture.transport.view() == "one\n",
           "the retained bytes are retried and delivered before command two");
    result = fixture.drain();
    expect(result.step == Step::Complete && fixture.recorder.calls == 2 &&
               fixture.transport.view() == "one\ntwo\n",
           "the second command runs only after the first drained");
}

const mm::test::case_ cases[]{
    {"one complete line", &runs_one_complete_line},
    {"multiline constructs", &accumulates_multiline_constructs},
    {"empty lines and editing", &empty_lines_and_editing},
    {"malformed input", &malformed_input_is_discarded},
    {"overlong input", &overlong_input_is_reported_once},
    {"feed while executing", &feeding_is_refused_while_executing},
    {"staged output drains first", &staged_output_drains_before_the_next_command},
};

const mm::test::registrar reg{"mm.shell session", cases};

}  // namespace
