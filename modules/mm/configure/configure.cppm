// Reusable configure
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

export module mm.configure;

export namespace mm::configure {

// Configuration options
[[nodiscard]] std::optional<std::string> get(std::string_view name);
[[nodiscard]] bool set(std::string_view name, std::string_view value, bool overwrite = true);
[[nodiscard]] bool unset(std::string_view name);

enum class CompilerSelection { Host, Cross };

// The persisted fields for one compiler role. Compiler names are deliberately
// absent: the model derives a Compiler's name from its invocation.
struct CompilerSettings {
    std::string invocation;
    std::string target;
    std::string platform;
    std::string compile_flags;
    std::string link_flags;
};

// Values written to project-root/out/config.mdy. A native configuration has no
// cross compiler and selects Host; selecting Cross requires cross settings.
struct Settings {
    std::string name = "default";
    CompilerSelection target_compiler = CompilerSelection::Host;
    CompilerSettings host;
    std::optional<CompilerSettings> cross;
    std::filesystem::path host_build_directory = "out/host";
    std::filesystem::path target_build_directory = "out/host";
};

// Writes out/config.mdy through out/config.mdy.tmp, then atomically replaces
// the destination. Invalid settings or an I/O failure leave an existing file
// unchanged.
[[nodiscard]] bool write_configuration(const std::filesystem::path& project_root,
                                       const Settings& settings);

}  // namespace mm::configure
