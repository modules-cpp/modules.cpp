// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>

export module mm.fs.conformance;

import mm.fs;

export namespace mm::fs::conformance {

struct Report {
    unsigned int passed = 0;
    unsigned int failed = 0;
    // A static description of the first failed check, empty when none failed.
    std::string_view first_failure;
};

// The scratch directory's name beneath the prefix.
inline constexpr std::string_view scratch_name = "mm-fs-conformance";

// Runs every check inside a scratch directory beneath prefix, removing it
// first if an earlier run left it, and again afterwards. Ok when the checks
// could run, whatever they found; otherwise the status of making the scratch
// directory, with report untouched.
[[nodiscard]] Status run(std::string_view prefix, Report& report);

}
