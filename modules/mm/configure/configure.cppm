// Reusable configure
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module mm.configure;

export namespace mm::configure {

// Configuration options
[[nodiscard]] std::optional<std::string> get(std::string_view name);
[[nodiscard]] bool set(std::string_view name, std::string_view value, bool overwrite = true);
[[nodiscard]] bool unset(std::string_view name);

enum class CompilerFamily { Gcc, Clang };
enum class CompilerSelection { Host, Cross };
enum class Build { Debug, Release };

struct BuildDefaults {
    int optimize;
    bool debug_info;
    bool assertions;
};
[[nodiscard]] BuildDefaults build_defaults(Build build);

enum class OptionType { Boolean, Number, Directory };
enum class OptionOrigin { Default, Assignment, Reset };

// Neutral input: the adapter copies declarations from an already parsed tree.
struct OptionNode {
    std::filesystem::path manifest;
    std::filesystem::path directory;
    std::string name;
    std::string kind;
    std::size_t parent = static_cast<std::size_t>(-1);
    std::vector<std::string> options;
    std::vector<std::string> resets;
    std::vector<std::string> read_only;
};

struct OptionValue {
    OptionType type = OptionType::Boolean;
    bool boolean = false;
    std::int64_t number = 0;
    std::string directory;
    bool unset = false;
    OptionOrigin origin = OptionOrigin::Default;
    std::filesystem::path value_source;
    bool read_only = false;
    std::filesystem::path lock_source;
};
using OptionValues = std::map<std::string, OptionValue, std::less<>>;

[[nodiscard]] bool resolve_options(const std::filesystem::path& project_root, Build build,
                                   const std::vector<OptionNode>& nodes,
                                   std::vector<OptionValues>& resolved);
[[nodiscard]] bool write_option_records(const std::filesystem::path& project_root, Build build,
                                        const std::vector<OptionNode>& nodes,
                                        const std::vector<OptionValues>& resolved, bool verbose);

// A compiler selector accepted by configure. The C-driver spellings gcc and
// clang are normalized to their C++ drivers so the same invocation can compile
// and link C++ programs. requested_major is present for a versioned selector.
struct CompilerRequest {
    CompilerFamily family = CompilerFamily::Gcc;
    std::string invocation = "g++";
    std::optional<unsigned> requested_major;
};

// Accepts gcc, g++, clang, or clang++, optionally followed by -<major>.
[[nodiscard]] std::optional<CompilerRequest> parse_compiler(std::string_view value);
[[nodiscard]] std::string_view compiler_family_name(CompilerFamily family);
[[nodiscard]] std::optional<Build> parse_build(std::string_view value);
[[nodiscard]] std::string_view build_name(Build build);
[[nodiscard]] std::string_view build_compile_flags(Build build);
[[nodiscard]] std::string_view build_link_flags(Build build);

// The persisted fields for one compiler role. family selects compiler-specific
// module behavior; invocation preserves the exact, possibly versioned C++
// driver selected by the user.
struct CompilerSettings {
    CompilerFamily family = CompilerFamily::Gcc;
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
    Build build = Build::Debug;
    CompilerSelection target_compiler = CompilerSelection::Host;
    CompilerSettings host;
    std::optional<CompilerSettings> cross;
    std::filesystem::path host_build_directory = "out/host";
    std::filesystem::path target_build_directory = "out/host";
};

// The configuration summary shared by build and test. Build and compiler
// values are strings so this module does not depend on either tool's types.
struct ConfigurationLog {
    std::string_view tool;
    std::filesystem::path configuration_path = "out/config.mdy";
    std::string_view build = "debug";
    std::string_view compiler_family;
    std::string_view compiler;
    std::string_view compile_flags;
    std::string_view link_flags;
    std::filesystem::path target;
    bool verbose = false;
};

// Prints whether configuration is persisted or default, the compiler details
// in verbose mode, and the selected target directory. Reports an inaccessible
// configuration path with the calling tool's name and returns false.
[[nodiscard]] bool log_configuration(const ConfigurationLog& log);

// Writes out/config.mdy through an exclusively created temporary directory
// beside it, then atomically replaces the destination. Invalid settings or
// an I/O failure leave an existing file unchanged.
[[nodiscard]] bool write_configuration(const std::filesystem::path& project_root,
                                       const Settings& settings);

}  // namespace mm::configure
