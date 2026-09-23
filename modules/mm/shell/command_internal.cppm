// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <span>

export module mm.shell:command_internal;

import :command;

export namespace mm::shell::detail {

// This partition is importable only by units of mm.shell. Applications use
// Registry::install and Registry::install_pack, neither of which admits a
// SpecialBuiltin descriptor.
struct StandardCommandInstaller {
    [[nodiscard]] static InstallResult install(
        Registry& registry,
        std::span<const CommandDescriptor> pack);
};

}  // namespace mm::shell::detail
