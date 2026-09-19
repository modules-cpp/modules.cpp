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

// The compatibility header a sketch application receives when it declares
// sketch-library. A vendored library includes it unconditionally under the
// name its own toolchain provides; this is that header, written beside
// main.cpp and built on mm.sketch. Generated, committed, and verified the
// same way main.cpp is.
[[nodiscard]] std::string sketch_header();

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
    std::string& error);

}

