// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.ino;

import mm.mdy;

export namespace mm::ino {

struct SourceFile {
    std::string path;
    std::string content;
};

struct Diagnostic {
    std::string file;
    std::size_t line = 0;
    std::string message;
    bool is_warning = false;
};

struct TransformResult {
    bool ok = true;
    std::string output;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] TransformResult transform(std::span<const SourceFile> sources);

// The same, for an application in the legacy profile: the prelude also brings
// mm::sketch::legacy into scope, which is what makes the Arduino core's looser
// signatures resolve. transform(sources) is transform(sources, false).
[[nodiscard]] TransformResult transform(std::span<const SourceFile> sources,
                                        bool legacy);

// The profile an application manifest selects. sketch-profile: legacy is the
// legacy profile; without the key the application is in the core profile.
inline constexpr std::string_view sketch_profile_key = "sketch-profile";
inline constexpr std::string_view legacy_profile_name = "legacy";
[[nodiscard]] bool is_legacy_application(const mm::mdy::MDYDocument& doc);

// A manifest's text with sketch-profile: legacy added to its front matter, or
// unchanged when the key is already there. Empty when the text has no front
// matter to add it to.
[[nodiscard]] std::string with_legacy_profile(std::string_view manifest);

// The compatibility header every sketch application receives. main.cpp
// includes it, and a vendored library includes it unconditionally under the
// name its own toolchain provides; this is that header, written beside
// main.cpp and built on mm.sketch. Generated, committed, and verified the
// same way main.cpp is.
// The name of the generated compatibility header, which is what main.cpp
// includes and what sketch_alias_headers forwards to.
[[nodiscard]] std::string_view sketch_header_name();

[[nodiscard]] std::string sketch_header();
// The legacy profile's compatibility header, which also brings
// mm::sketch::legacy into scope for the library sources that include it.
[[nodiscard]] std::string sketch_header(bool legacy);

// The names a vendored library includes by, each one a header its own
// toolchain ships separately and this project keeps in one place. Each is
// generated beside the compatibility header and forwards to it, so a library
// reaching for Print.h, Wire.h, or the name its whole ecosystem writes finds
// the same declarations under the name it wrote.
[[nodiscard]] std::span<const std::string_view> sketch_alias_headers();
[[nodiscard]] std::string sketch_alias_header(std::string_view name);

[[nodiscard]] bool check_application(const std::filesystem::path& app_dir,
                                     const mm::mdy::MDYDocument& doc,
                                     std::string& error);

[[nodiscard]] bool write_guarded(const std::filesystem::path& app_dir,
                                 std::string_view target_filename,
                                 std::string_view content,
                                 std::string& error,
                                 std::string_view temp_filename = "");

struct LibraryAppNode {
    std::filesystem::path dir;
    std::string rel_path;
    std::string name;
    std::vector<std::string> sketches;
    std::string sketch_library_rel;
    bool legacy = false;   // written as sketch-profile: legacy
};

struct LibraryDirNode {
    std::filesystem::path dir;
    std::string name;
    std::vector<std::string> folders;
    bool is_root = false;
};

struct LibraryPlan {
    bool ok = true;
    std::string error;
    std::vector<std::string> skipped;
    LibraryDirNode root_node;
    std::vector<LibraryDirNode> dir_nodes;
    std::vector<LibraryAppNode> app_nodes;
};

[[nodiscard]] bool is_library_root(const std::filesystem::path& dir);

[[nodiscard]] LibraryPlan discover_library(
    const std::filesystem::path& library_root);

[[nodiscard]] std::string render_root_manifest(
    const LibraryDirNode& root,
    std::string_view project_rel = "");

[[nodiscard]] std::string render_dir_manifest(
    const LibraryDirNode& node);

[[nodiscard]] std::string render_app_manifest(
    const LibraryAppNode& node);

[[nodiscard]] bool validate_manifest_compatibility(
    const mm::mdy::MDYDocument& doc,
    const LibraryDirNode* dir_node,
    const LibraryAppNode* app_node,
    bool expect_project,
    const std::filesystem::path& library_root,
    const std::filesystem::path& manifest_path,
    std::string& error,
    const std::filesystem::path& expected_project_root = "");

}

