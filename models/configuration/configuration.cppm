// Abstract data model of the project's build configuration. See
// docs/modules-model.mdy for the full models/ picture.
//
// Every accessor here is declared, intended policy, not a measurement of
// what actually ran: nothing in this type observes or records a real
// invocation. Three accessors need that distinction spelled out because they
// could otherwise be mistaken for effective, verified state:
//
//   - locale(): "C" is not set, checked, or enforced anywhere in this
//     repository - nothing calls setlocale(3) or exports LC_ALL/LANG. It
//     states what a plain POSIX environment provides absent an override,
//     which every script and tool here assumes rather than pins. Treat it
//     as declared policy a reader can rely on being the intent, not as
//     evidence of the process's actual locale at any given run.
//   - persisted(): whether these values came from out/config.mdy or from the
//     unconfigured default that build and test fall back to.
//   - build()/compiler_family()/compiler()/compiler_flags()/linker_flags():
//     these mirror mm::build::BuildConfiguration and Toolchain
//     (modules/mm/build/build.cppm). The unconfigured default is a debug build
//     with GCC through g++; configure persists one debug or release build and
//     one exact GCC or Clang C++ driver in out/config.mdy, and build and test
//     resolve that same file. bootstrap.sh and build0 deliberately remain a
//     fixed recovery path through plain "c++". A Configuration describes the
//     self-hosted rule, not bootstrap's fixed one.
//
// platform() and shell() are comparatively safe: mm::build::run always
// execs /bin/sh regardless of $SHELL (docs/modules.mdy, mm.shell), and
// "Process execution and bootstrap scripts require POSIX services" is an
// existing documented boundary (docs/modules.mdy's "Current boundaries"
// section) - both are closer to actually-true-everywhere than locale() is,
// but still declared policy rather than something this type measures.
//
// selection(), build_directory() and host_build_directory() describe the lane
// rather than the compiler: which of a toolchain's compilers produces target
// artifacts, and where each lane writes. The directory names themselves are
// configure policy, not fixed here.
//
// A Configuration is an input to running a Tool, not a structural fact about
// the repository, so nothing else here needs to depend on it. It imports the
// toolchain abstraction whose programs and arguments it selects.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string_view>

export module models.configuration;

import models.toolchain;

export namespace models {

enum class CompilerSelection { Host, Cross };
enum class Build { Debug, Release };

class Configuration {
public:
    virtual ~Configuration() = default;

    // The persisted configuration name, e.g. "gcc-debug".
    [[nodiscard]] virtual std::string_view name() const = 0;

    [[nodiscard]] virtual bool persisted() const = 0;

    [[nodiscard]] virtual CompilerSelection selection() const = 0;

    // The host toolchain is always present. target_toolchain() follows the
    // selection rather than record presence: it is null while Host is selected,
    // even if the persisted file also carries an unused cross record.
    [[nodiscard]] virtual const Toolchain& host_toolchain() const = 0;
    [[nodiscard]] virtual const Toolchain* target_toolchain() const = 0;
    [[nodiscard]] virtual const Toolchain* configured_target_toolchain() const = 0;

    // Root relative. build_directory() is the selected lane's; the host lane's
    // is always available, since programs that run during a build are host
    // artifacts whatever selection() is. target_build_directory() is empty
    // when no target toolchain is configured.
    [[nodiscard]] virtual std::filesystem::path build_directory() const = 0;
    [[nodiscard]] virtual std::filesystem::path host_build_directory() const = 0;
    [[nodiscard]] virtual std::filesystem::path target_build_directory() const = 0;

    // The one build selected for every compiling tool in the project.
    [[nodiscard]] virtual Build build() const = 0;

    // The exact C++ driver, e.g. "g++-15" or "clang++-20". Compiler-specific
    // module arguments are execution policy and are not part of this value.
    [[nodiscard]] virtual std::string_view compiler() const = 0;

    // Selects compiler-specific module artifact and invocation behavior.
    [[nodiscard]] virtual CompilerFamily compiler_family() const = 0;

    // Common flags, e.g. "-std=c++20". The selected backend adds its module
    // artifact flags for each translation unit.
    [[nodiscard]] virtual std::string_view compiler_flags() const = 0;

    // e.g. "-std=c++20".
    [[nodiscard]] virtual std::string_view linker_flags() const = 0;

    // Whether a run should echo the commands it executes.
    [[nodiscard]] virtual bool verbose() const = 0;

    // Fixed project policy, not derived from the environment: "POSIX".
    [[nodiscard]] virtual std::string_view platform() const = 0;

    // Fixed, declared project policy: "C". Not measured or enforced -
    // nothing in this repository calls setlocale(3) or sets LC_ALL/LANG.
    // See the class comment above.
    [[nodiscard]] virtual std::string_view locale() const = 0;

    // Fixed project policy: "/bin/sh", what mm::build::run always execs
    // through regardless of $SHELL.
    [[nodiscard]] virtual std::string_view shell() const = 0;
};

}  // namespace models
