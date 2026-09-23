// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:execute;

import :command;
import :expand;
import :io;
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
};

struct EvaluatorFrame {
    FrameKind kind = FrameKind::List;
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

private:
    struct Outcome {
        bool returns = false;
        StepResult result;
    };

    enum class Drain { Done, Advanced, Blocked, Failed };

    [[nodiscard]] Status push(FrameKind kind, std::size_t node, bool tested,
                              bool negate);
    void pop();
    [[nodiscard]] Outcome enter_command(std::size_t node, bool tested);
    [[nodiscard]] Outcome run_simple(std::size_t node, bool negate);
    [[nodiscard]] CommandResult assign_prefixes(std::size_t node,
                                                std::size_t count);
    [[nodiscard]] Status append_item(std::string_view value);
    [[nodiscard]] Status build_pattern(std::size_t node,
                                       std::size_t& length);
    [[nodiscard]] bool unwind_to_loop(bool pop_loop);
    void complete_frame(int status);
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
    MemorySink staging_out_{};
    MemorySink staging_error_{};
    std::size_t drained_out_ = 0;
    std::size_t drained_error_ = 0;
    SinkFailure drain_failure_{};
    StepResult held_{};
    bool holding_ = false;
    int last_status_ = 0;
    bool tested_ = false;
    bool active_ = false;
};

}  // namespace mm::shell
