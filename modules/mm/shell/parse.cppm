// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>

export module mm.shell:parse;

import :source;
import :status;
import :syntax;

export namespace mm::shell {

enum class ParseStatus { Complete, Incomplete, Malformed, Overflow };

struct MeasureOutcome {
    ParseStatus status = ParseStatus::Complete;
    ParseCounts required;
    SourceLocation issue;
};

struct ParseOutcome {
    ParseStatus status = ParseStatus::Complete;
    ParseCounts required;
    SourceLocation issue;
    OverflowInfo overflow;
};

[[nodiscard]] MeasureOutcome measure_embedded(SourceView source);

// Source must remain immutable between preflight and this call. On failure,
// out and all five caller storage spans are left unchanged.
[[nodiscard]] ParseOutcome parse_embedded(
    SourceView source, ScriptStorage storage, EmbeddedScript& out);

}  // namespace mm::shell
