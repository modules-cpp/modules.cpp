// Reusable host and target application execution.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

export module mm.run;

import mm.build;

export namespace mm::run {

// Constructs the safely quoted command for a host executable or configured
// target runner. extra_runner_arguments are inserted after the runner's
// configured prefix and before its executable image. nullopt means the
// selected target lane has no runner, or host execution incorrectly supplied
// runner arguments.
[[nodiscard]] std::optional<std::string> command(
    const mm::build::Toolchain& toolchain, bool target_lane,
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments = {},
    const std::vector<std::string>& extra_runner_arguments = {});

// Executes command() through the project's common shell process wrapper.
// Returns -1 when command construction is unavailable.
int execute(const mm::build::Toolchain& toolchain, bool target_lane,
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments = {});

}
