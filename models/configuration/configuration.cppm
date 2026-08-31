// Abstract data model of the project's build configuration. See
// docs/modules-model.mdy for the full models/ picture.
//
// The model represents the whole project concept, not only what today's
// tooling happens to enforce at runtime: compiler(), compiler_flags(), and
// linker_flags() mirror mm::build::Toolchain (modules/mm/build/build.cppm)
// exactly, honoring $CXX when set, but platform(), locale(), and shell()
// state fixed project policy that is real and documented even though
// nothing calls setlocale(3) or execs a shell other than /bin/sh to enforce
// it. mm::build::run always execs /bin/sh regardless of $SHELL
// (docs/modules.mdy, mm.shell), and "Process execution and bootstrap
// scripts require POSIX services" is an existing documented boundary,
// docs/modules.mdy's "Current boundaries" section; the C locale is what a
// plain POSIX environment provides absent an override, which is what every
// script and tool here assumes rather than pins. These three are implied
// and fixed: they do not vary per invocation the way compiler() and
// verbose() do.
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

class Configuration {
public:
    virtual ~Configuration() = default;

    // e.g. "c++ -fmodules-ts".
    [[nodiscard]] virtual std::string_view compiler() const = 0;

    // e.g. "-std=c++20 -x c++".
    [[nodiscard]] virtual std::string_view compiler_flags() const = 0;

    // e.g. "-std=c++20".
    [[nodiscard]] virtual std::string_view linker_flags() const = 0;

    // Whether a run should echo the commands it executes.
    [[nodiscard]] virtual bool verbose() const = 0;

    // Fixed project policy, not derived from the environment: "POSIX".
    [[nodiscard]] virtual std::string_view platform() const = 0;

    // Fixed project policy, not derived from the environment: "C".
    [[nodiscard]] virtual std::string_view locale() const = 0;

    // Fixed project policy: "/bin/sh", what mm::build::run always execs
    // through regardless of $SHELL.
    [[nodiscard]] virtual std::string_view shell() const = 0;
};

}  // namespace models
