// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module mm.build:config;

import mm.configure;

// Toolchains, the selected-lane record, the shared exit-code vocabulary, and the processor table keyed on the compiler family.

export namespace mm::build {

// Exit codes shared by every tool built on this module. 
inline constexpr int exit_ok       = 0;
inline constexpr int exit_usage    = 64;
inline constexpr int exit_manifest = 65;
inline constexpr int exit_unavailable = 77;
inline constexpr int exit_compile  = 80;
inline constexpr int exit_link     = 81;
inline constexpr int exit_run      = 127;

using CompilerFamily = mm::configure::CompilerFamily;
using Build = mm::configure::Build;

struct ToolchainProgram {
    std::string invocation;
    std::string arguments;
};

using RunnerImage = mm::configure::RunnerImage;

struct ToolchainRunner {
    std::string invocation;
    std::vector<std::string> prefix_arguments;
    RunnerImage image = RunnerImage::Positional;
    std::string image_option;
    std::vector<std::string> image_arguments;
    std::vector<std::string> suffix_arguments;
    bool forwards_arguments = true;
};

using DebuggerConnection = mm::configure::DebuggerConnection;

struct ToolchainDebugger {
    std::string invocation;
    std::vector<std::string> prefix_arguments;
    DebuggerConnection connection = DebuggerConnection::Direct;
    std::string remote_endpoint;
    std::vector<std::string> runner_arguments;
};

struct Toolchain {
    CompilerFamily family = CompilerFamily::Gcc;
    std::string target = "host";
    ToolchainProgram compiler = {
        "g++", std::string(mm::configure::build_compile_flags(mm::configure::Build::Debug))};
    ToolchainProgram assembler = {"g++", {}};
    ToolchainProgram linker = {
        "g++", std::string(mm::configure::build_link_flags(mm::configure::Build::Debug))};
    ToolchainProgram librarian;
    ToolchainProgram c_compiler;
    std::optional<ToolchainDebugger> debugger;
    std::optional<ToolchainRunner> runner;
    bool verbose = false;
};

using Platform = mm::configure::PlatformSettings;

using mm::configure::compiler_family_name;
using mm::configure::build_name;

// The unconfigured project default: a debug build with GCC. Compiler and build
// selection are persisted by configure rather than chosen by each process.
Toolchain default_toolchain(bool verbose = false);

// The host lane and any cross record retained from out/config.mdy. Selection
// is private so the object cannot select a missing cross lane; current build
// front ends execute selected_toolchain(). Malformed or unreadable
// configuration returns false, while callers separately decide whether an
// absent file means fallback.
class BuildConfiguration {
public:
    Build build = Build::Debug;
    std::filesystem::path build_directory;        // the selected lane's output
    std::filesystem::path host_build_directory;

    [[nodiscard]] const Toolchain& host_toolchain() const { return host_; }
    [[nodiscard]] const Toolchain* cross_toolchain() const {
        return cross_ ? &*cross_ : nullptr;
    }
    [[nodiscard]] const std::filesystem::path* cross_build_directory() const {
        return cross_build_directory_ ? &*cross_build_directory_ : nullptr;
    }
    [[nodiscard]] bool selects_cross() const { return selects_cross_; }
    [[nodiscard]] bool target_has_host_capability() const {
        return target_has_host_capability_;
    }
    [[nodiscard]] const Platform& host_platform() const { return host_platform_; }
    [[nodiscard]] const Platform* target_platform() const {
        return selects_cross_ ? configured_target_platform() : nullptr;
    }
    [[nodiscard]] const Platform* configured_target_platform() const {
        return cross_platform_ ? &*cross_platform_ : nullptr;
    }
    [[nodiscard]] bool configuration_2() const { return configuration_2_; }

    [[nodiscard]] const Toolchain* toolchain_for(bool target) const {
        if (target) return cross_toolchain();
        return &host_;
    }
    [[nodiscard]] const std::filesystem::path* build_directory_for(bool target) const {
        if (target) return cross_build_directory();
        return &host_build_directory;
    }

    // The loader is the only writer of the lane selection, so selects_cross_
    // can never be true without a cross_ value for these accessors to return.
    [[nodiscard]] const Toolchain& selected_toolchain() const {
        return selects_cross_ ? cross_.value() : host_;
    }
    [[nodiscard]] Toolchain& selected_toolchain() {
        return selects_cross_ ? cross_.value() : host_;
    }

private:
    Toolchain host_;
    std::optional<Toolchain> cross_;
    std::optional<std::filesystem::path> cross_build_directory_;
    Platform host_platform_;
    std::optional<Platform> cross_platform_;
    bool selects_cross_ = false;
    bool target_has_host_capability_ = false;
    bool configuration_2_ = false;

    friend bool load_configuration(const std::filesystem::path&, bool, BuildConfiguration&);
    friend bool resolve_configuration(const std::filesystem::path&, bool, BuildConfiguration&);
};

[[nodiscard]] bool load_configuration(const std::filesystem::path& path,
                                      bool verbose,
                                      BuildConfiguration& configuration);

// Loads project_root/out/config.mdy when present; otherwise returns the shared
// debug GCC default and the legacy out build directory. Every compiling front
// end uses this resolver so build and test cannot choose different values.
[[nodiscard]] bool resolve_configuration(const std::filesystem::path& project_root,
                                         bool verbose,
                                         BuildConfiguration& configuration);

struct ProcessorEntry {
    CompilerFamily family;
    std::string_view target;
    std::string_view cpu;
    std::string_view instruction_set;
    std::string_view float_abi;
    std::string_view security_domain;
    std::string_view arguments[4];
};

constexpr ProcessorEntry processor_table[] = {
    // Hosted rows, one per family because the table is keyed on the compiler
    // that will be invoked. A native lane wants no processor argument at all,
    // so both families contribute an empty list and differ only in the key; a
    // board selected with clang would otherwise be rejected as an unknown
    // processor combination for a difference that has no effect on any command.
    {CompilerFamily::Gcc, "aarch64-linux-gnu", "aarch64", "native", "native",
     "non-secure", {}},
    {CompilerFamily::Clang, "aarch64-linux-gnu", "aarch64", "native", "native",
     "non-secure", {}},
    {CompilerFamily::Gcc, "x86_64-linux-gnu", "x86_64", "native", "native",
     "non-secure", {}},
    {CompilerFamily::Clang, "x86_64-linux-gnu", "x86_64", "native", "native",
     "non-secure", {}},
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m3", "thumb", "soft", "non-secure",
     {"-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft"}},
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m0plus", "thumb", "soft", "non-secure",
     {"-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft"}},
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33", "thumb", "softfp", "non-secure",
     {"-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=softfp"}},
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33", "thumb", "softfp", "secure",
     {"-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=softfp", "-mcmse"}},
    // RP2350's Hazard3 cores, measured with the Pico SDK toolchain. GCC 16.1
    // accepts the SDK's preferred CPU profile, so project objects use the same
    // selection rather than the SDK's compatibility fallback.
    {CompilerFamily::Gcc, "riscv32-pico-elf", "hazard3",
     "rv32imacb_zicsr_zifencei_zmmul_zaamo_zalrsc_zca_zcb_zcmp_zba_zbb_zbkb_zbs_xh3bextm",
     "soft", "non-secure", {"-mcpu=hazard3-rp2350", "-mstrict-align"}},
};

std::size_t processor_argument_count(const ProcessorEntry& entry) {
    std::size_t count = 0;
    while (count < std::size(entry.arguments) && !entry.arguments[count].empty()) ++count;
    return count;
}

const ProcessorEntry* find_processor_entry(
    CompilerFamily family,
    std::string_view target,
    std::string_view cpu,
    std::string_view instruction_set,
    std::string_view float_abi,
    std::string_view security_domain) {
    if (security_domain.empty()) security_domain = "non-secure";
    for (const auto& entry : processor_table) {
        if (entry.family == family && entry.target == target &&
            entry.cpu == cpu && entry.instruction_set == instruction_set &&
            entry.float_abi == float_abi && entry.security_domain == security_domain) {
            return &entry;
        }
    }
    return nullptr;
}

const ProcessorEntry* find_processor_entry_by_arguments(
    CompilerFamily family,
    std::string_view target,
    const std::vector<std::string>& arguments) {
    for (const auto& entry : processor_table) {
        if (entry.family != family || entry.target != target ||
            arguments.size() != processor_argument_count(entry))
            continue;
        bool equal = true;
        for (std::size_t i = 0; i < arguments.size(); ++i)
            if (arguments[i] != entry.arguments[i]) equal = false;
        if (equal) return &entry;
    }
    return nullptr;
}

}
