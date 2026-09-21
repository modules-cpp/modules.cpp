// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module mm.build:external;

import :config;
import :compile;
import :manifest;

// The external CMake lane: toolchain and inputs files, package requirements, ABI probes, and the external link.

export namespace mm::build {

// Formats a value as a CMake bracket argument, selecting an equals count up to
// max_equals that does not appear in the payload. Returns nullopt if value
// contains a newline, carriage return, or semicolon, or exceeds the bounded length.
[[nodiscard]] std::optional<std::string> cmake_bracket_argument(std::string_view value,
                                                                std::size_t max_equals = 10);

// Generates mm-toolchain.cmake for external CMake builds. Carries compiler identity,
// sysroot, try-compile static library, and CMAKE_SYSTEM_NAME (Linux for hosted lanes,
// Generic for bare-metal), and carries no processor flags.
[[nodiscard]] bool write_toolchain_cmake(const std::filesystem::path& destination,
                                         const Toolchain& toolchain,
                                         const Platform& platform);

// Generates mm-inputs.cmake for external CMake builds.
[[nodiscard]] bool write_inputs_cmake(
    const std::filesystem::path& destination,
    const std::vector<std::filesystem::path>& objects,
    const std::string& output_name,
    const std::filesystem::path& library_source,
    const std::string& board_name,
    const std::vector<std::string>& board_chain,
    const std::filesystem::path& toolchain_file,
    const std::filesystem::path& c_compiler,
    const std::filesystem::path& cxx_compiler);

struct CMakePackageRequirement {
    std::string variable;
    std::filesystem::path directory;
};

// Reads an optional checked-in mm-requires.txt from a bridge directory. Each
// line names one environment/CMake package variable ending in _DIR. Required
// directories and conventional config-package files are validated without
// executing foreign code; returned paths are canonical.
[[nodiscard]] bool read_cmake_package_requirements(
    const std::filesystem::path& bridge_directory,
    std::vector<CMakePackageRequirement>& requirements,
    std::string_view tool = "build");

// Reads the bridge's compile_commands.json through mm.json and finds the
// one entry whose file resolves to canonical_probe. The entry must carry a
// command string whose driver resolves to recorded_c_driver; its -m options
// are kept in sanitised_options. A document that does not parse is reported
// with its line and column.
[[nodiscard]] bool extract_probe_options(
    const std::filesystem::path& compile_commands_file,
    const std::filesystem::path& canonical_probe,
    std::string_view recorded_c_driver,
    std::vector<std::string>& sanitised_options,
    std::string_view tool);

struct ProjectionSchema {
    std::string_view target_triple;
    std::vector<std::string_view> fields;
};

[[nodiscard]] const ProjectionSchema* find_projection_schema(std::string_view target_triple);

[[nodiscard]] bool query_driver_projection(
    const std::string& c_driver,
    const std::vector<std::string>& sanitised_options,
    const ProjectionSchema& schema,
    std::map<std::string, std::string>& projection,
    std::string_view tool);

// Parses one LC_ALL=C GCC --help=target listing against a closed schema.
// Value fields may be present with an empty value: GCC uses that representation
// when an equivalent architecture spelling leaves a CPU name unset.
[[nodiscard]] bool parse_driver_projection(
    std::string_view output,
    const ProjectionSchema& schema,
    std::map<std::string, std::string>& projection,
    std::string_view tool);

[[nodiscard]] bool publish_external_results(
    const std::filesystem::path& results_file,
    const std::filesystem::path& external_dir,
    const std::string& output_name,
    const std::filesystem::path& target_output,
    const ArtifactContext& context,
    std::string_view tool);
[[nodiscard]] bool publish_external_results(
    const std::filesystem::path& results_file,
    const std::filesystem::path& external_dir,
    const std::string& output_name,
    const std::filesystem::path& target_output,
    std::string_view tool);

[[nodiscard]] int external_link(
    const Project& project,
    const Platform& platform,
    const Toolchain& toolchain,
    const std::string& app_name,
    const std::vector<std::filesystem::path>& objects,
    const ArtifactContext& context,
    const std::filesystem::path& target_output,
    bool verbose = false);
[[nodiscard]] int external_link(
    const Project& project,
    const Platform& platform,
    const Toolchain& toolchain,
    const std::string& app_name,
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& build_dir,
    const std::filesystem::path& target_output,
    bool verbose = false);

}
