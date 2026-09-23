// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string>
#include <vector>

export module mm.shell.full:redirect;

import :syntax;

export namespace mm::shell::full {

enum class RedirectionKind {
    Input, Truncate, Append, HereDocument, DuplicateInput,
    DuplicateOutput,
};

struct RedirectionStep {
    RedirectionKind kind = RedirectionKind::Input;
    unsigned int target = 0;
    // Still raw shell spelling: expansion occurs before any service call.
    std::string operand;
    // For HereDocument only. Delimiter quoting disables expansion, and
    // neither variant performs field splitting or pathname expansion.
    std::string body;
    bool expand_body = false;
};

struct RedirectionPlan {
    std::vector<RedirectionStep> steps;
    Diagnostic diagnostic;

    [[nodiscard]] bool ok() const {
        return diagnostic.status == ParseStatus::Complete;
    }
};

// Planning has no observable I/O. The evaluator must expand every operand
// and verify every service request before altering the active descriptors.
[[nodiscard]] RedirectionPlan plan_redirections(
    const FullScript& script, std::size_t command_node);

}  // namespace mm::shell::full
