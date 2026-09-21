// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module mm.build:compile;

import :config;
import :manifest;

// The artifact context, and the compile, link, install, and module-cache drivers.

export namespace mm::build {

class ArtifactContext {
public:
    ArtifactContext() = default;
    ArtifactContext(std::filesystem::path tree_root,
                    std::filesystem::path output_root,
                    std::filesystem::path tools_dir = {},
                    bool external = false);

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] const std::filesystem::path& tree_root() const {
        return tree_root_;
    }
    [[nodiscard]] const std::filesystem::path& output_root() const {
        return output_root_;
    }
    [[nodiscard]] const std::filesystem::path& tools_dir() const {
        return tools_dir_;
    }
    [[nodiscard]] bool is_external() const { return external_; }

    [[nodiscard]] std::string prefix(const BuildableNode& node) const;
    [[nodiscard]] std::string prefix(bool node_external) const;

    [[nodiscard]] std::filesystem::path object_path(
        const BuildableNode& node, const TranslationUnit& unit) const;
    [[nodiscard]] std::filesystem::path bmi_dir() const;
    [[nodiscard]] std::filesystem::path executable_path(
        const BuildableNode& node) const;
    [[nodiscard]] std::filesystem::path bridge_dir(std::string_view library,
                                                   std::string_view board,
                                                   std::string_view name) const;
    [[nodiscard]] std::filesystem::path board_object_path(
        const std::filesystem::path& board_source,
        std::string_view board_name = "") const;

    [[nodiscard]] bool check_artifact_path(
        const std::filesystem::path& path) const;
    [[nodiscard]] bool check_install_path(
        const std::filesystem::path& destination,
        const std::filesystem::path& path) const;

    void record_objects(const BuildableNode& target,
                        std::vector<std::filesystem::path> objects);
    [[nodiscard]] const std::vector<std::filesystem::path>& objects(
        const BuildableNode& target) const;

    void record_board_objects(std::string_view board_name,
                              std::vector<std::filesystem::path> objects);
    [[nodiscard]] const std::vector<std::filesystem::path>& board_objects(
        std::string_view board_name = "") const;

private:
    std::filesystem::path tree_root_;
    std::filesystem::path output_root_;
    std::filesystem::path tools_dir_;
    bool external_ = false;
    bool valid_ = true;

    std::map<std::pair<bool, std::filesystem::path>,
             std::vector<std::filesystem::path>> target_objects_;
    std::map<std::string, std::vector<std::filesystem::path>> board_objects_;
};

// Quotes a path for /bin/sh. Uses single quotes: $(), backticks and $NAME all
// still expand inside double quotes, so a path is not safe merely for being
// wrapped in them.
std::string shell_quote(const std::filesystem::path& path);

// Runs a command through /bin/sh, returning its exit code rather than a wait
// status. Every path interpolated into the command must go through shell_quote.
int run(const Toolchain& toolchain, const std::string& command);

// Compiles every source of a target, appending to target.objects. Library
// include directories are separate from the configured compiler argument
// string so each path remains one shell-quoted argument.
int compile(const Toolchain& toolchain, BuildableNode& target,
            ArtifactContext& context,
            const std::vector<std::filesystem::path>& include_directories = {});
int compile(const Toolchain& toolchain, BuildableNode& target,
            const std::filesystem::path& build_dir,
            const std::vector<std::filesystem::path>& include_directories = {});

// Links objects directly and in order: self registering test suites live in
// static initialisers and an archive would discard them.
int link(const Toolchain& toolchain,
         const std::vector<std::filesystem::path>& objects,
         const std::filesystem::path& output,
         const std::vector<std::string>& link_inputs = {},
         const ArtifactContext* context = nullptr);

// Copies a built binary into bin_dir, unlinking first so a tool can replace the
// binary it is running from.
int install(const std::filesystem::path& from,
            const std::filesystem::path& bin_dir, const std::string& name,
            const std::filesystem::path& tree_root = {});

// A stale module interface silently contradicts the sources being compiled.
// Clears this output lane's module artifacts, recreates its private BMI
// directory, and writes the GCC module mapper for every named unit in tree.
// Keeping GCC CMIs beneath build_dir prevents concurrent lanes from deleting
// or replacing one another's default project-root gcm.cache.
[[nodiscard]] bool prepare_module_cache(
    const Toolchain& toolchain, const Tree& tree,
    const ArtifactContext& context);
[[nodiscard]] bool prepare_module_cache(
    const Toolchain& toolchain, const Tree& tree,
    const std::filesystem::path& build_dir = "out");

}
