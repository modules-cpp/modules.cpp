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
// The legacy profile's names: the core ones, avr/pgmspace.h, which an
// Arduino core for a board other than AVR ships for code written for AVR, and
// pins_arduino.h and wiring_private.h, which every Arduino core ships.
[[nodiscard]] std::span<const std::string_view> sketch_alias_headers(bool legacy);
[[nodiscard]] std::string sketch_alias_header(std::string_view name);

[[nodiscard]] bool check_application(const std::filesystem::path& app_dir,
                                     const mm::mdy::MDYDocument& doc,
                                     std::string& error);

// Whether file may be written as generated output: it is absent, or its
// first line is the header sketch writes. A file of the same name that a
// sketch folder ships itself, such as a main.cpp compiled beside the .ino by
// another toolchain, is never overwritten.
[[nodiscard]] bool is_absent_or_generated(const std::filesystem::path& file);

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
    // The other sketch libraries the example uses, as paths relative to its
    // directory, written as further sketch-library: entries after the first.
    std::vector<std::string> sibling_library_rels;
    // The C and C++ sources beside the sketch, which the Arduino tools compile
    // with it, written as further file: entries after main.cpp.
    std::vector<std::string> extra_sources;
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

// The environment variable naming the folders searched for the other sketch
// libraries an example uses, separated by colons as PATH is.
inline constexpr std::string_view sketch_libraries_path_variable =
    "MM_SKETCH_LIBRARIES_PATH";

// The folders searched, in order: the folder holding library_root, whose
// other libraries were installed beside it and so come first, as a
// sketchbook's libraries come first to the Arduino tools; then each entry of
// value (the variable's text). Each folder holds libraries as subdirectories; an
// entry that is itself a sketch library stands for that one library. Empty
// and repeated entries are dropped.
[[nodiscard]] std::vector<std::filesystem::path> sketch_library_search_path(
    std::string_view value, const std::filesystem::path& library_root);
// The same, reading the variable from the environment.
[[nodiscard]] std::vector<std::filesystem::path> sketch_library_search_path(
    const std::filesystem::path& library_root);

// One sketch library a search found: its root, the name it declares (name=
// in library.properties, else the directory name), the directory its headers
// are included from (src/ when it has one), and its depends= names.
struct SketchLibraryEntry {
    std::filesystem::path root;
    std::string name;
    std::filesystem::path include_dir;
    std::vector<std::string> depends;
};

// Reads one directory as a sketch library: a directory with a regular
// library.properties or library.json. Returns false for anything else.
[[nodiscard]] bool read_sketch_library(const std::filesystem::path& dir,
                                       SketchLibraryEntry& entry);

// Every sketch library in the search path, in search order, each once.
[[nodiscard]] std::vector<SketchLibraryEntry> index_sketch_libraries(
    std::span<const std::filesystem::path> search_path);

struct SiblingResolution {
    // Roots of the other libraries, in the order they were found.
    std::vector<std::filesystem::path> libraries;
    // depends= names no library in the search path declares.
    std::vector<std::string> unresolved;
};

// The other sketch libraries an example uses, as the Arduino tools find
// them. The example's own files (its sketches and any header or source beside
// them) are scanned for include directives, and each header that neither the
// example, a library already chosen, nor the compatibility headers provide is
// looked up in the index: the library whose include directory holds it, one
// whose name matches the header's stem first. Then each chosen library's
// depends= names, the exercised library's among them, are looked up by name,
// transitively. Only the example's files are scanned, never a library's, so
// an include a library guards for another board chooses nothing.
[[nodiscard]] SiblingResolution resolve_sibling_libraries(
    const std::filesystem::path& app_dir, const SketchLibraryEntry& library,
    std::span<const SketchLibraryEntry> index);

// manifest with a sketch-library: line for each of entries it lacks, after
// its last sketch-library: line. Nothing is removed. Returns empty when the
// text has no front matter or no sketch-library: line.
[[nodiscard]] std::string with_sketch_libraries(
    std::string_view manifest, std::span<const std::string> entries);
// The same for file: lines, after the last file: line.
[[nodiscard]] std::string with_files(
    std::string_view manifest, std::span<const std::string> entries);

// The C and C++ sources in a sketch folder other than main.cpp and anything
// sketch generated, sorted: the files the Arduino tools compile beside the
// sketch.
[[nodiscard]] std::vector<std::string> sketch_folder_sources(
    const std::filesystem::path& app_dir);

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

