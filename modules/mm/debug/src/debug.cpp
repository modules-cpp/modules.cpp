// Reusable debugger orchestration implementation.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string>
#include <vector>

module mm.debug;

namespace mm::debug {
namespace {

void append(std::string& command, const std::filesystem::path& argument) {
    command += " " + mm::build::shell_quote(argument);
}

std::string runner_command(const mm::build::ToolchainRunner& runner,
                           const mm::build::ToolchainDebugger& debugger,
                           const std::filesystem::path& executable,
                           const std::vector<std::string>& arguments) {
    std::string command = mm::build::shell_quote(std::filesystem::path(runner.invocation));
    for (const auto& argument : runner.prefix_arguments) append(command, argument);
    for (const auto& argument : debugger.runner_arguments) append(command, argument);
    if (runner.image == mm::build::RunnerImage::Option) append(command, runner.image_option);
    append(command, executable);
    for (const auto& argument : runner.suffix_arguments) append(command, argument);
    if (runner.forwards_arguments)
        for (const auto& argument : arguments) append(command, argument);
    return command;
}

std::string debugger_command(const mm::build::ToolchainDebugger& debugger,
                             const std::filesystem::path& executable,
                             const std::vector<std::string>& arguments) {
    std::string command = mm::build::shell_quote(std::filesystem::path(debugger.invocation));
    for (const auto& argument : debugger.prefix_arguments) append(command, argument);
    if (debugger.connection == mm::build::DebuggerConnection::Direct) {
        append(command, "--args");
        append(command, executable);
        for (const auto& argument : arguments) append(command, argument);
    } else {
        append(command, executable);
        append(command, "-ex");
        append(command, "target remote " + debugger.remote_endpoint);
    }
    return command;
}

}

int execute(const mm::build::Toolchain& toolchain, bool target_lane,
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments) {
    if (!toolchain.debugger) return -1;
    const auto& debugger = *toolchain.debugger;

    if (debugger.connection == mm::build::DebuggerConnection::Direct) {
        if (target_lane) return -1;
        return mm::build::run(toolchain, debugger_command(debugger, executable, arguments));
    }
    if (!target_lane || !toolchain.runner) return -1;

    // The fixed shell fragment manages only the PID it just started. Every
    // configured or user-derived value in both commands is single-quoted.
    std::string command = runner_command(*toolchain.runner, debugger, executable, arguments);
    command += " & mm_debug_runner_pid=$!; "
               "trap 'kill \"$mm_debug_runner_pid\" 2>/dev/null; "
               "wait \"$mm_debug_runner_pid\" 2>/dev/null' 0; ";
    command += debugger_command(debugger, executable, arguments);
    return mm::build::run(toolchain, command);
}

}
