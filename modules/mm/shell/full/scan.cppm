// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.shell.full:scan;

import :syntax;

export namespace mm::shell::full {

// The scanner owns no state beyond its FullScript output. On failure the
// partial token list remains available for a diagnostic or CLI token dump.
[[nodiscard]] Diagnostic scan_full(std::string_view text,
                                   FullScript& output);

}  // namespace mm::shell::full
