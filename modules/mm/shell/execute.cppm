// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:execute;

import :command;
import :expand;
import :source;
import :state;
import :status;
import :syntax;

export namespace mm::shell {

enum class Step { Running, Yielded, Complete, Failed };

struct EvaluatorStorage {
    WordExpansionStorage expansion;
    std::span<std::string_view> arguments;
    std::span<char> argument_text;
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

private:
    const EmbeddedScript* script_ = nullptr;
    Registry* registry_ = nullptr;
    CommandContext* context_ = nullptr;
    EvaluatorStorage storage_{};
    std::size_t list_node_ = 0;
    std::size_t list_link_ = 0;
    std::size_t andor_node_ = 0;
    std::size_t command_link_ = 0;
    int last_status_ = 0;
    bool active_ = false;
};

}  // namespace mm::shell
