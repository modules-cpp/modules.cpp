// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:session;

import :command;
import :execute;
import :parse;
import :source;
import :status;
import :syntax;

export namespace mm::shell {

enum class SessionState {
    // Waiting for the first byte of a command.
    Ready,
    // A construct is open, so the next physical line continues it.
    Continuing,
    // A script is parsed and its evaluator is running.
    Executing,
    // The buffered line overflowed and is discarded to its line ending.
    Discarding,
};

struct SessionStorage {
    // Accumulated source. An EmbeddedScript views these bytes, so the span
    // must outlive execution and must not alias parser or evaluator storage.
    std::span<char> source;
    ScriptStorage script;
    EvaluatorStorage evaluator;
};

struct FeedResult {
    Status status = Status::Ok;
    // Bytes taken from the caller's span. A started script stops the feed at
    // the line ending that completed it, so the caller resumes from here.
    std::size_t consumed = 0;
    SessionState state = SessionState::Ready;
    // A parse diagnostic for the discarded line, reported exactly once.
    ParseStatus parse_status = ParseStatus::Complete;
    SourceLocation issue;
    OverflowInfo overflow;

    [[nodiscard]] constexpr bool ok() const { return status == Status::Ok; }
    [[nodiscard]] constexpr bool executing() const {
        return state == SessionState::Executing;
    }
};

// Session accumulates source a byte at a time and runs each complete command
// through one Evaluator. It recognizes printable bytes, horizontal tab,
// backspace, delete, CR, and LF, and silently ignores every other control
// byte. Tab is accepted because dropping it would join adjacent tokens.
//
// There is no history, completion, cursor movement, ANSI handling, or
// transport loop: the application polls its own transport and alternates
// feed() and step(). An overlong command is discarded and reported once; it is
// never executed as a truncated script.
class Session {
public:
    [[nodiscard]] Status begin(Registry& registry, CommandContext& context,
                               SessionStorage storage);
    [[nodiscard]] FeedResult feed(std::span<const char> bytes);
    [[nodiscard]] StepResult step(std::size_t operation_budget = 32);

    [[nodiscard]] SessionState state() const { return state_; }
    [[nodiscard]] bool executing() const {
        return state_ == SessionState::Executing;
    }
    [[nodiscard]] std::size_t buffered() const { return used_; }
    [[nodiscard]] int last_status() const { return last_status_; }
    // Discards buffered source and abandons any running script.
    void reset();

private:
    [[nodiscard]] bool append(char byte);
    [[nodiscard]] FeedResult launch();

    Registry* registry_ = nullptr;
    CommandContext* context_ = nullptr;
    SessionStorage storage_{};
    EmbeddedScript script_{};
    Evaluator evaluator_;
    std::size_t used_ = 0;
    SessionState state_ = SessionState::Ready;
    int last_status_ = 0;
};

}  // namespace mm::shell
