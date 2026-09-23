// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

module mm.shell.posix;

import :environment;

namespace mm::shell {

std::filesystem::path current_shell() {
    const char* value = std::getenv("SHELL");
    if (value == nullptr) return {};
    return std::filesystem::path(value);
}

std::optional<std::string> get(std::string_view name) {
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

bool set(std::string_view name, std::string_view value, bool overwrite) {
    const std::string key(name);
    const std::string val(value);
    return ::setenv(key.c_str(), val.c_str(), overwrite ? 1 : 0) == 0;
}

bool unset(std::string_view name) {
    const std::string key(name);
    return ::unsetenv(key.c_str()) == 0;
}

}  // namespace mm::shell
