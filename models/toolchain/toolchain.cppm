// Abstract data model of the programs and arguments assigned to one target's
// build roles. A role is a job rather than an executable: several roles may
// return the same Tool, and an unbound role returns nullptr.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>
#include <vector>

export module models.toolchain;

import models.tool;

export namespace models {

enum class CompilerFamily { Gcc, Clang };
enum class ToolRole { Compiler, Assembler, Linker, Librarian, Debugger };
enum class RunnerImage { Positional, Option };
enum class DebuggerConnection { Direct, RunnerRemote };

class Debugger {
public:
    virtual ~Debugger() = default;
    [[nodiscard]] virtual const Tool& program() const = 0;
    [[nodiscard]] virtual std::vector<std::string_view> prefix_arguments() const = 0;
    [[nodiscard]] virtual DebuggerConnection connection() const = 0;
    [[nodiscard]] virtual std::string_view remote_endpoint() const = 0;
    [[nodiscard]] virtual std::vector<std::string_view> runner_arguments() const = 0;
};

class Runner {
public:
    virtual ~Runner() = default;
    [[nodiscard]] virtual const Tool& program() const = 0;
    [[nodiscard]] virtual std::vector<std::string_view> prefix_arguments() const = 0;
    [[nodiscard]] virtual RunnerImage image() const = 0;
    [[nodiscard]] virtual std::string_view image_option() const = 0;
    [[nodiscard]] virtual std::vector<std::string_view> suffix_arguments() const = 0;
    [[nodiscard]] virtual bool forwards_arguments() const = 0;
};

class Toolchain {
public:
    virtual ~Toolchain() = default;

    [[nodiscard]] virtual std::string_view target() const = 0;
    [[nodiscard]] virtual CompilerFamily family() const = 0;
    [[nodiscard]] virtual const Tool* program(ToolRole role) const = 0;
    [[nodiscard]] virtual std::string_view arguments(ToolRole role) const = 0;

    // Describes the build implementation, not configuration data. compile
    // invokes Compiler and link invokes Linker; no other role is run directly.
    [[nodiscard]] virtual bool invoked(ToolRole role) const = 0;
    [[nodiscard]] virtual const Debugger* debugger() const = 0;
    [[nodiscard]] virtual const Runner* runner() const = 0;
};

}  // namespace models
