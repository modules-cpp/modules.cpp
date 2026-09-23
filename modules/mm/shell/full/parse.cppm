// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>

export module mm.shell.full:parse;

import :syntax;

export namespace mm::shell::full {

struct ParseResult {
    FullScript script;
    Diagnostic diagnostic;

    [[nodiscard]] bool ok() const {
        return diagnostic.status == ParseStatus::Complete;
    }
};

[[nodiscard]] ParseResult parse_full(std::string_view text);

}  // namespace mm::shell::full
