// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:pathname;

import :service;

export namespace mm::shell::full {

struct PathnameResult {
    ServiceStatus service = ServiceStatus::Ok;
    // Sorted byte-wise, which is what a shell in the POSIX locale produces.
    std::vector<std::string> matches;
    // False when the word held no unquoted pattern byte, or when it held one
    // and nothing matched. Either way the caller uses the word unchanged,
    // which is the POSIX rule for an unmatched pattern.
    bool expanded = false;

    [[nodiscard]] bool ok() const {
        return service == ServiceStatus::Ok;
    }
};

// True when the word contains a pattern byte the shell would act on. Bytes
// already quoted by the caller are not pattern bytes, so the caller marks them
// by passing quoted positions, not by stripping them.
[[nodiscard]] bool has_pattern(std::string_view word);

// Matches one already-expanded, quote-removed word against the directory tree
// the service reports. directory is the working directory a relative word is
// resolved against; it is never prepended to the results. No byte is read and
// nothing is created: only names are requested.
[[nodiscard]] PathnameResult expand_pathname(std::string_view word,
                                             std::string_view directory,
                                             FileService files);

}  // namespace mm::shell::full
