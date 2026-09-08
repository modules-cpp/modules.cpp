// Reusable host and target application execution implementation.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

module mm.run;

namespace mm::run {
namespace {

void append(std::string& result, const std::filesystem::path& argument) {
    result += " " + mm::build::shell_quote(argument);
}

}

std::optional<std::string> command(
    const mm::build::Toolchain& toolchain, bool target_lane,
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::vector<std::string>& extra_runner_arguments) {
    if (!target_lane) {
        if (!extra_runner_arguments.empty()) return std::nullopt;
        std::string result = mm::build::shell_quote(executable);
        for (const auto& argument : arguments) append(result, argument);
        return result;
    }
    if (!toolchain.runner) return std::nullopt;

    const auto& runner = *toolchain.runner;
    std::string result = mm::build::shell_quote(std::filesystem::path(runner.invocation));
    for (const auto& argument : runner.prefix_arguments) append(result, argument);
    for (const auto& argument : extra_runner_arguments) append(result, argument);
    if (runner.image == mm::build::RunnerImage::Option) append(result, runner.image_option);
    append(result, executable);
    for (const auto& argument : runner.suffix_arguments) append(result, argument);
    if (runner.forwards_arguments)
        for (const auto& argument : arguments) append(result, argument);
    return result;
}

int execute(const mm::build::Toolchain& toolchain, bool target_lane,
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments) {
    const auto result = command(toolchain, target_lane, executable, arguments);
    return result ? mm::build::run(toolchain, *result) : -1;
}

}
