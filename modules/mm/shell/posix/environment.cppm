// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

export module mm.shell.posix:environment;

export namespace mm::shell {

// The shell named by $SHELL: this process's interactive shell, not the one
// mm::build::run invokes. POSIX system(), which run wraps, always uses
// /bin/sh regardless of $SHELL. Empty if $SHELL is unset.
[[nodiscard]] std::filesystem::path current_shell();

// Wraps the POSIX getenv/setenv/unsetenv functions <cstdlib> exposes on this
// platform, so callers reach the process environment through one interface
// rather than each holding its own #include <cstdlib>. set and unset
// mutate this process's real environment, so a command mm::build::run
// launches afterward inherits the change.
[[nodiscard]] std::optional<std::string> get(std::string_view name);
[[nodiscard]] bool set(
    std::string_view name, std::string_view value, bool overwrite = true);
[[nodiscard]] bool unset(std::string_view name);

}  // namespace mm::shell
