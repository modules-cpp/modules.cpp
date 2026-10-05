// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell.fs;

import mm.shell;

export namespace mm::shell::fs {

// The prefixes the application mounted -- "/data", "/sd" -- for df and
// mounts, since mm.fs keeps no list of its own. It must outlive the registry
// entries.
struct FsBinding {
    std::span<const std::string_view> prefixes;
};

constexpr std::size_t fs_builtin_count = 10;

// Adds Files to a capability set; the commands need it.
void enable(CapabilitySet& capabilities);

// Installs the ten descriptors as one transaction, beside whatever the
// registry already holds. binding must outlive the registry entries.
[[nodiscard]] InstallResult install_fs(Registry& registry, FsBinding& binding);

}  // namespace mm::shell::fs
