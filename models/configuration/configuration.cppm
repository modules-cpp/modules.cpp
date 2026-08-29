// Abstract data model of a compiler configuration. See models.manifest
// (models/manifest/manifest.cppm) for the note on why this is a design
// artifact rather than a build target.
//
// Mirrors mm::build::Toolchain (modules/mm/build/build.cppm): the compiler
// command and flags a build or test run uses, honoring $CXX when set.
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
};

}  // namespace models
