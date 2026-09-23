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
// This is not an installation: it exists for introspection and for focused
// tests that dispatch descriptors directly. Registry::install refuses the
// special-builtin class, so a pack materialized here cannot be installed
// through the public entry points.
//
// Every descriptor except the introspection commands is stateless, so the
// span is safe to copy.
[[nodiscard]] InstallResult core_builtins(Introspection& binding,
                                         std::span<CommandDescriptor> slots,
                                         std::size_t& count);

// Installs the complete level-1 pack into registry, all or none. It is the
// only way an application obtains the special builtins, and it runs before
// any custom command so that a custom name may not take one of theirs.
// binding.registry is set to registry, and binding must outlive it.
[[nodiscard]] InstallResult install_level1(Registry& registry,
                                          Introspection& binding);

// Atomically installs the level-1 pack and a later level's descriptors.
// extras cannot claim the SpecialBuiltin class. scratch is caller-owned and
// needs core_builtin_count + extras.size() slots. No registry entry changes
// when preflight fails.
[[nodiscard]] InstallResult install_level1_with(
    Registry& registry, Introspection& binding,
    std::span<const CommandDescriptor> extras,
    std::span<CommandDescriptor> scratch);

// True for the core run descriptor. The evaluator resolves that one itself,
// because invoking an installed script pushes a positional call frame and no
// handler can do that.
[[nodiscard]] bool invokes_script(const CommandDescriptor& descriptor);

}  // namespace mm::shell
