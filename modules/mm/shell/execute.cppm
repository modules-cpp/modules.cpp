// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:execute;

import :command;
import :expand;
import :function;
import :io;
import :script;
import :pattern;
import :source;
import :state;
import :status;
import :syntax;

export namespace mm::shell {

enum class Step { Running, Yielded, Complete, Failed };

// One frame is open for each active structural production. Frames identify
// syntax indices, so no frame holds a C++ call frame or a resumable lambda.
enum class FrameKind {
    List,
    AndOr,
    If,
    While,
    For,
    Case,
    Brace,
    Function,
    Script,
    // Resolves a simple command's command substitutions before expanding it.
    Substitute,
    // Runs one nested list against the child state and capture sink.
    Capture,
};

struct EvaluatorFrame {
    FrameKind kind = FrameKind::List;
    // A function body is a separate EmbeddedScript owned by the
    // FunctionLibrary, so every frame names the script its indices belong to.
    const EmbeddedScript* script = nullptr;
    std::size_t node = 0;
    // Secondary syntax index: the elif branch or case item under inspection.
    std::size_t aux = 0;
    std::size_t link = 0;
    std::size_t phase = 0;
    std::size_t cursor = 0;
    // Loop-word arena marks reclaimed when this frame pops.
    std::size_t item_mark = 0;
    std::size_t text_mark = 0;
    std::size_t item_first = 0;
    std::size_t item_count = 0;
    // Call frames only: where this call's saved positional slots start, and
    // what the caller's positional parameters were.
    std::size_t slot_mark = 0;
    std::size_t slot_count = 0;
    PositionalFrame positionals;
    // Substitution frames only: the capture arena marks this frame releases.
    std::size_t capture_text_mark = 0;
    std::size_t capture_value_first = 0;
    std::size_t capture_value_count = 0;
    int status = 0;
    bool negate = false;
    bool tested = false;
};

struct EvaluatorStorage {
    WordExpansionStorage expansion;
    std::span<std::string_view> arguments;
    std::span<char> argument_text;
    std::span<EvaluatorFrame> frames;
    // Loop items and case selectors outlive one command, so they are copied
    // out of expansion scratch into this arena.
    std::span<std::string_view> loop_items;
    std::span<char> loop_text;
    std::span<PatternByte> pattern;
    // Required only when a simple command carries prefix assignments whose
    // persistence the descriptor's class denies. Must not alias state or
    // expansion storage.
    std::span<VariableSlot> prefix_variables;
    std::span<char> prefix_variable_text;
    // Evaluator-owned output scratch. A handler writes into an all-or-fail
    // staging sink and the evaluator drains it to the application's sink over
    // later step calls. An empty span makes that stream write straight
    // through, which suits a memory sink that never blocks.
    std::span<char> staged_output;
    std::span<char> staged_error;
    // Function definition and lookup are available only when a library is
    // supplied. Without one a definition reports Status::Unsupported.
    FunctionLibrary* functions = nullptr;
    // Installed scripts resolve by name and through run only when a library
    // is supplied.
    ScriptLibrary* scripts = nullptr;
    // Saved caller positional slots, one contiguous run per open call frame.
    std::span<PositionalSlot> call_positionals;
    // Open call frames allowed at once, so a runaway recursion is refused
    // rather than exhausting the frame span.
    std::size_t call_limit = 16;
    // Command substitution. Every span below is required for $(list) to run;
    // leaving any of them empty keeps a substitution fragment
    // Status::Unsupported. capture_text and capture_values are bump arenas
    // released with the substitution frame that filled them.
    std::span<char> capture_text;
    std::span<std::string_view> capture_values;
    // The child's forked variables. Must not alias state or expansion
    // storage.
    std::span<VariableSlot> capture_variables;
    std::span<char> capture_variable_text;
    // The nested list is parsed at invocation, because the outer parse only
    // measured it.
    std::span<ScriptToken> capture_tokens;
    std::span<WordFragment> capture_fragments;
    std::span<SyntaxNode> capture_nodes;
    std::span<SyntaxLink> capture_links;
    std::span<ParserFrame> capture_parser_context;
    // Definitions made inside a substitution go here and disappear with the
    // child. Without one, defining a function inside $() is unsupported.
    FunctionLibrary* capture_functions = nullptr;
};

struct StepResult {
    Step step = Step::Running;
    CommandResult command;
};

class Evaluator {
public:
    [[nodiscard]] Status begin(const EmbeddedScript& script,
                               Registry& registry,
                               CommandContext& context,
                               EvaluatorStorage storage);
    [[nodiscard]] StepResult step(std::size_t operation_budget = 32);
    // True while the command that most recently ran occupied a tested
    // context: a condition list, an inverted command, or the left operand
    // of && or ||. set -e reads this instead of a bare nonzero status.
    [[nodiscard]] bool tested_context() const { return tested_; }
    [[nodiscard]] std::size_t frame_depth() const { return frame_count_; }
    // True while staged bytes have not reached the application's sink. The
    // result of the command that produced them is withheld until they do.
    [[nodiscard]] bool pending_output() const;

    // Instrumentation for the operation-unit contract. Each counter describes
    // the most recent call and is reset at its entry.
    //
    // Structural work is bounded by units: one syntax or frame transition,
    // one drain advance, or sixteen bytes of resumable evaluator copying.
    // A step never exceeds the budget it was given and never enters two
    // native handlers.
    [[nodiscard]] std::size_t last_step_units() const { return last_units_; }
    [[nodiscard]] std::size_t last_step_handlers() const {
        return last_handlers_;
    }
    // Argument-vector expansion for one simple command is bounded by the
    // caller's argv capacities rather than by units, the way the language
    // specification bounds handler work. These report what the last simple
    // command actually consumed of those capacities.
    [[nodiscard]] std::size_t last_command_slots() const {
        return last_slots_;
    }
    [[nodiscard]] std::size_t last_command_bytes() const {
        return last_bytes_;
    }

private:
    struct Outcome {
        bool returns = false;
        StepResult result;
    };

    enum class Drain { Done, Advanced, Blocked, Failed };

    [[nodiscard]] Status push(FrameKind kind, const EmbeddedScript& script,
                              std::size_t node, bool tested, bool negate);
    void pop();
    [[nodiscard]] Outcome enter_command(const EmbeddedScript& script,
                                       std::size_t node, bool tested);
    [[nodiscard]] Outcome run_simple(const EmbeddedScript& script,
                                     std::size_t node, bool negate);
    [[nodiscard]] Outcome define_function(const EmbeddedScript& script,
                                          std::size_t node);
    // Shared by a function body and an installed script: both are a
    // positional call frame over a separate EmbeddedScript.
    [[nodiscard]] Outcome enter_call(FrameKind kind,
                                     const EmbeddedScript& body,
                                     std::span<const std::string_view> args,
                                     bool negate);
    [[nodiscard]] CommandResult assign_prefixes(const EmbeddedScript& script,
                                                std::size_t node,
                                                std::size_t count,
                                                std::size_t& consumed);
    // The capture results this command has not consumed yet, in fragment
    // order. Empty when the command has no substitutions.
    [[nodiscard]] std::span<const std::string_view> captures(
        std::size_t consumed) const;
    [[nodiscard]] Status append_item(std::string_view value);
    [[nodiscard]] Status build_pattern(const EmbeddedScript& script,
                                       std::size_t node,
                                       std::size_t& length);
    [[nodiscard]] bool unwind_to_loop(bool pop_loop);
    [[nodiscard]] bool unwind_to_call(int status);
    void complete_frame(int status);
    // Drops every open frame and publishes a final status: exit, set -e, and
    // an unset parameter under set -u all end the evaluator this way.
    void abandon(int status);
    // The child state and sinks replace the application's while a capture
    // frame is open; everything else the evaluator reads is shared.
    [[nodiscard]] ShellState& state();
    [[nodiscard]] FunctionLibrary* functions();
    [[nodiscard]] std::size_t count_substitutions(
        const EmbeddedScript& script, std::size_t node,
        std::size_t assignments) const;
    [[nodiscard]] const WordFragment* substitution_at(
        const EmbeddedScript& script, std::size_t node,
        std::size_t assignments, std::size_t index) const;
    [[nodiscard]] Status begin_capture(const EmbeddedScript& script,
                                       const WordFragment& fragment);
    [[nodiscard]] Status finish_capture(EvaluatorFrame& frame);
    [[nodiscard]] IoServices handler_io();
    [[nodiscard]] Drain drain_once();
    void recycle_staging();
    [[nodiscard]] Outcome deliver(StepResult result);

    const EmbeddedScript* script_ = nullptr;
    Registry* registry_ = nullptr;
    CommandContext* context_ = nullptr;
    EvaluatorStorage storage_{};
    std::size_t frame_count_ = 0;
    std::size_t items_used_ = 0;
    std::size_t item_text_used_ = 0;
    std::size_t slots_used_ = 0;
    std::size_t calls_open_ = 0;
    ShellState capture_state_{};
    EmbeddedScript capture_script_{};
    MemorySink capture_sink_{};
    std::size_t capture_text_used_ = 0;
    std::size_t capture_values_used_ = 0;
    std::size_t capture_depth_ = 0;
    // The capture results belonging to the command being expanded.
    std::size_t capture_first_ = 0;
    std::size_t capture_count_ = 0;
    MemorySink staging_out_{};
    MemorySink staging_error_{};
    std::size_t drained_out_ = 0;
    std::size_t drained_error_ = 0;
    SinkFailure drain_failure_{};
    std::size_t last_units_ = 0;
    std::size_t last_handlers_ = 0;
    std::size_t last_slots_ = 0;
    std::size_t last_bytes_ = 0;
    StepResult held_{};
    bool holding_ = false;
    int last_status_ = 0;
    bool tested_ = false;
    bool active_ = false;
};

}  // namespace mm::shell
