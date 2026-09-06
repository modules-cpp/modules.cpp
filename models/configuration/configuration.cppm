// Abstract data model of the project's build configuration. See
// docs/modules-model.mdy for the full models/ picture.
//
// Every accessor here is declared, intended policy, not a measurement of
// what actually ran: nothing in this type observes or records a real
// invocation. Two accessors need that distinction spelled out because they
// could otherwise be mistaken for effective, verified state:
//
//   - locale(): "C" is not set, checked, or enforced anywhere in this
//     repository - nothing calls setlocale(3) or exports LC_ALL/LANG. It
//     states what a plain POSIX environment provides absent an override,
//     which every script and tool here assumes rather than pins. Treat it
//     as declared policy a reader can rely on being the intent, not as
//     evidence of the process's actual locale at any given run.
//   - compiler_family()/compiler()/compiler_flags()/linker_flags(): these
//     mirror mm::build::Toolchain (modules/mm/build/build.cppm). The
//     unconfigured default is GCC through g++; configure persists one exact
//     GCC or Clang C++ driver in out/config.mdy, and build and test resolve
//     that same file. bootstrap.sh and build0 deliberately remain a fixed
//     recovery path through plain "c++". A Configuration describes the
//     self-hosted rule, not bootstrap's fixed one.
//
// platform() and shell() are comparatively safe: mm::build::run always
// execs /bin/sh regardless of $SHELL (docs/modules.mdy, mm.shell), and
// "Process execution and bootstrap scripts require POSIX services" is an
// existing documented boundary (docs/modules.mdy's "Current boundaries"
// section) - both are closer to actually-true-everywhere than locale() is,
// but still declared policy rather than something this type measures.
//
// Foundational, like models.document: a Configuration is an input to
// running a Tool, not a structural fact about the repository, so nothing
// else here needs to depend on it, and it depends on nothing else here.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>

export module models.configuration;

export namespace models {

enum class CompilerFamily { Gcc, Clang };

class Configuration {
public:
    virtual ~Configuration() = default;

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
