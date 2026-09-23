// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:builtin;

import :command;
import :script;
import :status;

export namespace mm::shell {

// :, true, false, echo, printf, test, [, set, shift, break, continue,
// return, exit, yield, capability, help, command, and run.
constexpr std::size_t core_builtin_count = 18;

// What help and command describe. The caller owns it and it must outlive the
// descriptors, because they hold its address. A null script library simply
// leaves installed scripts out of the listing.
struct Introspection {
    Registry* registry = nullptr;
    ScriptLibrary* scripts = nullptr;
};

// Materializes the level-1 core command descriptors into caller-owned slots.
// This is not an installation: install_level1 installs the complete pack
// atomically once command substitution exists, so nothing here admits a
// partial pack to an application.
//
// Every descriptor except the introspection commands is stateless, so the
// span is safe to copy.
[[nodiscard]] InstallResult core_builtins(Introspection& binding,
                                         std::span<CommandDescriptor> slots,
                                         std::size_t& count);

// True for the core run descriptor. The evaluator resolves that one itself,
// because invoking an installed script pushes a positional call frame and no
// handler can do that.
[[nodiscard]] bool invokes_script(const CommandDescriptor& descriptor);

}  // namespace mm::shell
