// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell:builtin;

import :command;
import :status;

export namespace mm::shell {

// :, true, false, echo, printf, test, [, set, shift, break, continue,
// return, exit, yield, capability, help, and command.
constexpr std::size_t core_builtin_count = 17;

// Materializes the level-1 core command descriptors into caller-owned slots.
// This is not an installation: install_level1 installs the complete pack
// atomically once functions, installed scripts, and command substitution
// exist, so nothing here admits a partial pack to an application.
//
// Introspection commands are bound to the registry that will own them, so
// that registry must outlive the descriptors. Every descriptor is otherwise
// stateless and the whole span is safe to copy.
[[nodiscard]] InstallResult core_builtins(Registry& registry,
                                         std::span<CommandDescriptor> slots,
                                         std::size_t& count);

}  // namespace mm::shell
