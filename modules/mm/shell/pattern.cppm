// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <span>
#include <string_view>

export module mm.shell:pattern;

import :status;

export namespace mm::shell {

struct PatternByte {
    char value = 0;
    bool quoted = false;
};

struct PatternResult {
    Status status = Status::Ok;
    bool matched = false;
};

// Pattern bytes are supplied after expansion and quote removal. Quoted
// metacharacters are compared literally. No filesystem access occurs.
[[nodiscard]] PatternResult match_case_pattern(
    std::span<const PatternByte> pattern, std::string_view value);

}  // namespace mm::shell
