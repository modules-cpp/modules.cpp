// Reusable configure
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module mm.configure;

export namespace mm::configure {

enum class CompilerFamily { Gcc, Clang };
enum class CompilerSelection { Host, Cross };
enum class Build { Debug, Release };
enum class PlatformSystem { Posix, Linux, BareMetal, Unknown };
enum class PlatformRuntime { Unknown, Glibc, Newlib, Picolibc, None };
enum class Responsibility { ResetVector, InitialStack, MemoryLayout, RuntimeInit, Syscalls };

[[nodiscard]] std::string_view platform_system_name(PlatformSystem system);
[[nodiscard]] std::string_view platform_runtime_name(PlatformRuntime runtime);
[[nodiscard]] std::string_view responsibility_name(Responsibility responsibility);
[[nodiscard]] std::optional<PlatformSystem> target_system(std::string_view target);

struct BuildDefaults {
    int optimize;
    bool debug_info;
    bool assertions;
};
[[nodiscard]] BuildDefaults build_defaults(Build build);

enum class OptionType { Boolean, Number, Directory };
enum class OptionOrigin { Default, Assignment, Reset };

// Neutral input: the adapter copies declarations from an already parsed tree.
struct OptionNode {
    std::filesystem::path manifest;
    std::filesystem::path directory;
    std::string name;
    std::string kind;
    std::string module_name;              // kind:module only, for use: resolution
    std::vector<std::string> uses;        // module names this node depends on
    std::size_t parent = static_cast<std::size_t>(-1);
    std::vector<std::string> options;
    std::vector<std::string> resets;
    std::vector<std::string> read_only;
    std::string library;
};

struct OptionValue {
    OptionType type = OptionType::Boolean;
    bool boolean = false;
    std::int64_t number = 0;
    std::string directory;
    bool unset = false;
    OptionOrigin origin = OptionOrigin::Default;
    std::filesystem::path value_source;
    bool read_only = false;
    std::filesystem::path lock_source;
};
using OptionValues = std::map<std::string, OptionValue, std::less<>>;

[[nodiscard]] bool resolve_options(const std::filesystem::path& project_root, Build build,
                                   const std::vector<OptionNode>& nodes,
                                   std::vector<OptionValues>& resolved,
                                   std::string_view tool = "configure");
// Records are resolved per lane, so they live beside the artifacts they
// describe: output_directory is the lane's build directory, not out/.
[[nodiscard]] bool write_option_records(const std::filesystem::path& project_root,
                                        const std::filesystem::path& output_directory, Build build,
                                        const std::vector<OptionNode>& nodes,
                                        const std::vector<OptionValues>& resolved, bool verbose);

// A compiler selector accepted by configure. The C-driver spellings gcc and
// clang are normalized to their C++ drivers so the same invocation can compile
// and link C++ programs. requested_major is present for a versioned selector.
struct CompilerRequest {
    CompilerFamily family = CompilerFamily::Gcc;
    std::string invocation = "g++";
    std::string target_prefix;
    std::optional<unsigned> requested_major;
};

// Accepts native or target-prefixed GCC and Clang drivers, optionally versioned.
[[nodiscard]] std::optional<CompilerRequest> parse_compiler(std::string_view value);
[[nodiscard]] bool valid_target_triple(std::string_view value);
[[nodiscard]] std::string_view compiler_family_name(CompilerFamily family);
[[nodiscard]] std::optional<Build> parse_build(std::string_view value);
[[nodiscard]] std::string_view build_name(Build build);
[[nodiscard]] std::string_view build_compile_flags(Build build);
[[nodiscard]] std::string_view build_link_flags(Build build);

// The one place the configured output layout is decided. Bootstrap output stays
// in out/, which also holds the authoritative out/config.mdy; a configured lane
// never writes there. target_output_directory names a distinct tree per target
// so two lanes cannot share one set of artifacts.
[[nodiscard]] std::filesystem::path host_output_directory();
[[nodiscard]] std::filesystem::path target_output_directory(std::string_view target);

// The persisted fields for one compiler role. family selects compiler-specific
// module behavior; invocation preserves the exact, possibly versioned C++
// driver selected by the user.
struct CompilerSettings {
    CompilerFamily family = CompilerFamily::Gcc;
    std::string invocation;
    std::string target;
    std::string platform;
    std::string compile_flags;
    std::string link_flags;
    std::string c_compiler;
};

// Derives the candidate C driver spelling by inverting normalisation on the
// recorded C++ driver (e.g. g++ -> gcc, arm-none-eabi-g++ -> arm-none-eabi-gcc,
// clang++ -> clang).
[[nodiscard]] std::string candidate_c_compiler(std::string_view cpp_compiler);

struct CompilerProbe {
    CompilerFamily family = CompilerFamily::Gcc;
    std::string target_triple;
    std::string version;
};

// The host configure front end supplies command execution. Keeping that
// operation injected leaves this module free of POSIX process APIs when it is
// compiled as part of a target lane.
using DriverCommandRunner = bool (*)(const std::string& command, std::string& output);

// Probes a compiler driver for its family, target triple, and version through
// the supplied host-side command runner.
[[nodiscard]] std::optional<CompilerProbe> probe_compiler(
    std::string_view invocation, DriverCommandRunner run_command);

// Three-way probe for a candidate or overriding C driver against the C++ driver:
// checks compiler family == expected_family, target triple == C++ driver's, and
// version == C++ driver's.
[[nodiscard]] bool probe_c_compiler(std::string_view c_driver,
                                    std::string_view cpp_driver,
                                    CompilerFamily expected_family,
                                    std::string& error_message,
                                    DriverCommandRunner run_command);

enum class LinkOwnership { Project, External };

// A resolved lane platform. Optional values are genuinely absent for the host
// and for legacy configuration records; an empty string is never used as an
// alternate spelling of absence. Responsibility ownership is present only for
// configuration-2 bare-metal lanes.
struct PlatformSettings {
    std::string target = "host";
    PlatformSystem system = PlatformSystem::Posix;
    PlatformRuntime runtime = PlatformRuntime::Unknown;
    LinkOwnership link_ownership = LinkOwnership::Project;
    std::optional<std::string> sdk;
    std::optional<std::filesystem::path> sdk_manifest;
    CompilerFamily sdk_family = CompilerFamily::Gcc;
    std::optional<std::string> board;
    std::optional<std::filesystem::path> board_manifest;
    std::map<Responsibility, std::string> responsibility_owners;
    std::vector<Responsibility> unresolved;
    std::optional<std::filesystem::path> sysroot;
    std::optional<std::filesystem::path> runtime_prefix;
    std::string specs_argument;
    std::vector<std::string> compiler_arguments;
    std::string machine;
    std::filesystem::path linker_script;
    std::vector<std::filesystem::path> board_sources;
    bool models_responsibilities = false;
};

enum class RunnerImage { Positional, Option };

struct RunnerSettings {
    std::string invocation;
    std::vector<std::string> prefix_arguments;
    RunnerImage image = RunnerImage::Positional;
    std::string image_option;
    std::vector<std::string> image_arguments;
    std::vector<std::string> suffix_arguments;
    bool forwards_arguments = true;
};

struct RunnerMachineProfile {
    std::string_view profile;
    std::string_view target;
    std::string_view machine;
    std::string_view invocation;
};

inline constexpr RunnerMachineProfile runner_machine_table[] = {
    {"qemu-system", "arm-none-eabi", "mps2-an385", "qemu-system-arm"},
    {"openocd", "arm-none-eabi", "rp2040", "openocd"},
    {"openocd", "arm-none-eabi", "rp2350", "openocd"},
};

[[nodiscard]] inline const RunnerMachineProfile* find_runner_machine(
    std::string_view profile, std::string_view target, std::string_view machine) {
    for (const auto& entry : runner_machine_table) {
        if (entry.profile == profile && entry.target == target && entry.machine == machine)
            return &entry;
    }
    return nullptr;
}

[[nodiscard]] inline const RunnerMachineProfile* find_runner_machine_by_invocation(
    std::string_view invocation, std::string_view target, std::string_view machine) {
    for (const auto& entry : runner_machine_table) {
        if (entry.invocation == invocation && entry.target == target && entry.machine == machine)
            return &entry;
    }
    return nullptr;
}

enum class DebuggerConnection { Direct, RunnerRemote };

struct DebuggerSettings {
    std::string invocation;
    std::vector<std::string> prefix_arguments;
    DebuggerConnection connection = DebuggerConnection::Direct;
    std::string remote_endpoint;
    std::vector<std::string> runner_arguments;
};

// Resolves a named, statically supported runner profile for a target.
// "none" means no runner; an unknown or incompatible profile is rejected.
[[nodiscard]] std::optional<RunnerSettings> runner_profile(std::string_view profile,
                                                           std::string_view target,
                                                           const PlatformSettings* platform = nullptr);

// Resolves the gdb or openocd profile for one lane. Host debugging invokes gdb directly;
// the supported QEMU user target uses gdb-multiarch and the runner's remote
// stub; openocd targets use gdb-multiarch connecting to openocd on localhost:3333.
// Other target/profile combinations are rejected.
[[nodiscard]] std::optional<DebuggerSettings> debugger_profile(
    std::string_view profile, std::string_view target,
    const PlatformSettings* platform = nullptr);

// Values written to project-root/out/config.mdy. A native configuration has no
// cross compiler and selects Host; selecting Cross requires cross settings.
struct Settings {
    std::string name = "default";
    Build build = Build::Debug;
    CompilerSelection target_compiler = CompilerSelection::Host;
    bool target_has_host_capability = false;
    CompilerSettings host;
    std::optional<CompilerSettings> cross;
    std::optional<DebuggerSettings> host_debugger;
    std::optional<DebuggerSettings> cross_debugger;
    std::optional<RunnerSettings> cross_runner;
    PlatformSettings host_platform;
    std::optional<PlatformSettings> cross_platform;
    bool configuration_2 = false;
    std::filesystem::path host_build_directory = host_output_directory();
    std::filesystem::path target_build_directory = host_output_directory();
};

// The configuration summary shared by build and test. Build and compiler
// values are strings so this module does not depend on either tool's types.
struct ConfigurationLog {
    std::string_view tool;
    std::filesystem::path configuration_path = "out/config.mdy";
    std::string_view build = "debug";
    std::string_view compiler_family;
    std::string_view compiler;
    std::string_view compile_flags;
    std::string_view link_flags;
    std::string_view runner;
    std::string_view debugger;
    std::filesystem::path target;
    bool verbose = false;
};

// Prints whether configuration is persisted or default, the compiler details
// in verbose mode, and the selected target directory. Reports an inaccessible
// configuration path with the calling tool's name and returns false.
[[nodiscard]] bool log_configuration(const ConfigurationLog& log);

// Writes out/config.mdy through an exclusively created temporary directory
// beside it, then atomically replaces the destination. Invalid settings or
// an I/O failure leave an existing file unchanged.
[[nodiscard]] bool write_configuration(const std::filesystem::path& project_root,
                                       const Settings& settings);

}  // namespace mm::configure
