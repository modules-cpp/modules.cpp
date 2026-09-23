// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:expand;

import mm.shell;

export namespace mm::shell::full {

enum class TrimMode {
    ShortestPrefix, LongestPrefix, ShortestSuffix, LongestSuffix,
};

struct TrimResult {
    Status status = Status::Ok;
    std::string value;
};

[[nodiscard]] TrimResult trim_parameter(
    std::string_view value, std::string_view pattern, TrimMode mode);

// Hexadecimal literals are the only arithmetic addition at the full level.
// All operators, width checks, and variable semantics remain in mm.shell.
[[nodiscard]] ArithmeticResult evaluate_full_arithmetic(
    std::string_view expression, const ShellState& state);

enum class HereSegmentKind {
    Literal,
    // The inner source of a $(list). Running a list is the evaluator's job,
    // so planning hands the request back rather than performing it.
    Substitution,
};

struct HereSegment {
    HereSegmentKind kind = HereSegmentKind::Literal;
    std::string text;
};

struct HereBody {
    Status status = Status::Ok;
    std::vector<HereSegment> segments;
    // True when the delimiter was unquoted, so the body was expanded.
    bool expanded = false;
    // Where an unsupported or malformed form starts, for a diagnostic.
    std::size_t issue = 0;
};

// Expands a here-document body. A quoted delimiter yields the body verbatim
// as one literal segment. An unquoted one applies parameter and arithmetic
// expansion and the backslash escapes a here-document honours, and returns
// each command substitution as a request. Neither variant performs field
// splitting or pathname expansion, which is why the result is a byte stream
// rather than a field vector.
[[nodiscard]] HereBody expand_here_document(std::string_view body,
                                            bool expand,
                                            const ShellState& state);

}  // namespace mm::shell::full
