// Reusable debugger orchestration.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string>
#include <vector>

export module mm.debug;

import mm.build;

export namespace mm::debug {

// Starts the configured debugger for one built application. A direct
// debugger owns the executable with --args. A runner-remote debugger starts
// the configured runner's remote stub, connects the debugger to it, and
// cleans up the runner when the debugger exits. Returns -1 when the selected
// lane lacks a compatible debugger or runner.
int execute(const mm::build::Toolchain& toolchain, bool target_lane,
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments = {});

}
