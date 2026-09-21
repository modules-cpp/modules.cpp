// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module mm.build:manifest;

import mm.configure;
import mm.mdy;
import :config;

// The structural manifest-tree model: nodes, targets, SDK, board, and library definitions, and the manifest version rules.

export namespace mm::build {

// One translation unit. An explicit module name identifies an importable unit
// and its BMI; the primary .cppm of a kind:module target may instead inherit
// BuildableNode::module_name. GCC uses the names to generate its mapper and
// Clang uses them to name precompiled module output.
struct TranslationUnit {
    std::string path;         // root relative
    std::string module_name;  // optional explicit BMI/module name
    std::filesystem::path source; // absolute
};

// Splits a "file:" or "unit:" value: a path, optionally followed by whitespace
// and the module name that unit defines.
TranslationUnit parse_unit(std::string_view value);

// One buildable thing named by a manifest. Sources are stored root relative and
// in declared order, because declaration order is dependency order.
struct BuildableNode {
    std::string kind;                            // module | app | test | doc
    std::string name;
    std::string module_name;                     // kind:module only
    std::filesystem::path dir;                   // root relative
    std::filesystem::path source_dir;            // absolute
    std::filesystem::path logical_dir;           // relative to its own tree
    std::vector<TranslationUnit> sources;        // root relative
    std::vector<std::string> uses;               // module names
    std::string requires_board;                  // kind:app or kind:test only
    std::string library;                         // kind:module only
    bool platform_interface = false;             // kind:module only
    std::vector<std::string> sketches;           // kind:app with sketch:
    // Absolute roots of the sketch libraries a sketch application
    // exercises, in declared order. Each contributes an include directory and
    // the sources compiled into this application.
    std::vector<std::filesystem::path> sketch_libraries;
    std::vector<std::filesystem::path> objects;  // filled in by compile
    bool external = false;
    bool non_core = false;
};

// One platform-provider declaration: a portable interface module, and the
// provider module a selected SDK or board binds it to. Both names are module
// names; neither is derived from the SDK or board name.
struct PlatformProviderBinding {
    std::string interface_module;
    std::string provider_module;
};

struct SdkDefinition {
    std::size_t node = static_cast<std::size_t>(-1);
    std::string name;
    std::filesystem::path manifest;
    std::string target;
    CompilerFamily family = CompilerFamily::Gcc;
    // compiler-family: any. A hosted SDK that supplies no specs file, no
    // linker script, and no runtime prefix does not care which toolchain
    // builds it, and naming gcc there would refuse a clang lane for no reason.
    // A bare-metal SDK owns the link and cannot say this.
    bool family_agnostic = false;
    mm::configure::PlatformRuntime runtime = mm::configure::PlatformRuntime::Unknown;
    std::string specs_profile;
    std::filesystem::path specs_file;
    std::optional<std::filesystem::path> sysroot;
    std::optional<std::filesystem::path> runtime_prefix;
    std::vector<mm::configure::Responsibility> provides;
    std::string library;
    std::vector<PlatformProviderBinding> providers;
};

enum class LibraryPathBase { Source, BuildPrefix };

struct LibraryPath {
    LibraryPathBase base = LibraryPathBase::Source;
    std::filesystem::path path;
};

// A library manifest describes a vendored tree and its public interface. It is
// structural: it never becomes a BuildableNode and none of its foreign sources
// enter the project's source collection.
struct LibraryDefinition {
    std::size_t node = static_cast<std::size_t>(-1);
    std::string name;
    std::filesystem::path manifest;
    std::filesystem::path source;
    std::filesystem::path licence;
    std::vector<LibraryPath> include_directories;
    std::vector<LibraryPath> library_directories;
    std::vector<LibraryPath> link_archives;
    std::vector<std::string> link_inputs;
    std::string external_build;
    bool checkout_present = false;
};

struct BoardDefinition {
    std::size_t node = static_cast<std::size_t>(-1);
    std::string name;
    std::filesystem::path manifest;
    std::string sdk;
    std::string cpu;
    std::string instruction_set;
    std::string float_abi;
    std::string security_domain;
    std::string machine;
    std::filesystem::path linker_script;
    std::vector<std::filesystem::path> sources;
    std::vector<mm::configure::Responsibility> provides;
    std::vector<std::string> compiler_arguments;
    std::vector<PlatformProviderBinding> providers;
    std::string derives_from;
    std::vector<std::string> chain;
    std::filesystem::path declared_linker_script;
    std::vector<std::filesystem::path> declared_sources;
    std::vector<mm::configure::Responsibility> declared_provides;
    std::vector<PlatformProviderBinding> declared_providers;
};

struct Tree {
    std::vector<BuildableNode> targets;  // kind:module and kind:app
    std::vector<BuildableNode> tests;    // kind:test, collected but never built here
    std::vector<BuildableNode> docs;     // kind:doc, prose; reached by a walk, never built
    bool ok = true;
};

// One manifest node in the tree, including the kind:project and kind:dir manifests
// that load_tree only traverses through. This is the structural view: what
// exists and how it nests, rather than what to compile.
struct ManifestNode {
    std::filesystem::path manifest;  // relative to the walk root
    std::filesystem::path dir;       // relative to the walk root
    std::filesystem::path source_dir; // absolute
    std::filesystem::path logical_dir; // relative to its own tree
    std::string kind;
    std::string name;
    std::size_t parent = static_cast<std::size_t>(-1);  // -1 at the root
    std::vector<std::size_t> children;
    bool external = false;
    bool non_core = false;
};

inline constexpr std::size_t no_parent = static_cast<std::size_t>(-1);

// Caller-specific diagnostics; only configure opts into a strict tree.
struct LoadPolicy {
    std::string_view tool = "build";
    bool strict_tree = false;
    bool warn_options = false;
    std::optional<std::filesystem::path> external;
};

[[nodiscard]] bool validate_manifest_schema(const mm::mdy::MDYDocument& document,
                                            const std::filesystem::path& manifest,
                                            const LoadPolicy& policy = {});

[[nodiscard]] bool is_safe_board_name(std::string_view name);

// Every reachable manifest, parents before children. Sets ok on failure.
std::vector<ManifestNode> load_nodes(const std::filesystem::path& dir, bool& ok,
                                    const LoadPolicy& policy = {});

inline constexpr std::size_t no_target = static_cast<std::size_t>(-1);

// Everything one walk of a manifest tree can know, gathered in a single
// traversal: the structural nodes, each manifest's parsed document, and the
// targets built from them.
//
// This exists because reading a tree twice cannot be made consistent. Before
// it, a caller wanting both views called load_nodes and load_tree, which
// walked and parsed every manifest separately, so the two passes could
// observe different file contents if anything changed between them, and
// callers then had to re-pair the results by directory to recover what one
// traversal never separates. documents is parallel to nodes, and target
// gives each node its entry in targets, tests, or docs, chosen by that
// node's kind, or no_target for a kind:project, kind:dir, kind:sdk,
// kind:board, or kind:library node that builds nothing.
struct Project {
    std::vector<ManifestNode> nodes;
    std::vector<mm::mdy::MDYDocument> documents;  // parallel to nodes
    std::vector<std::size_t> target;              // parallel to nodes
    std::vector<BuildableNode> targets;           // kind:module and kind:app
    std::vector<BuildableNode> tests;
    std::vector<BuildableNode> docs;
    std::vector<SdkDefinition> sdks;
    std::vector<BoardDefinition> boards;
    std::vector<LibraryDefinition> libraries;
    bool ok = true;
};

// The one traversal. load_tree and load_nodes are projections of this.
Project load_project(const std::filesystem::path& dir, const LoadPolicy& policy = {});

std::filesystem::path resolve_manifest(std::filesystem::path path);

// Walks up until it finds the mm.mdy declaring kind: project. Empty if none.
std::filesystem::path find_project_root(std::filesystem::path dir);

struct ResolvedRoots {
    std::filesystem::path project_root;
    std::optional<std::filesystem::path> external_root;
    std::filesystem::path tools_dir;
    std::filesystem::path requested_manifest;
    std::filesystem::path requested_dir;
    std::string requested_node;
    bool ok = true;
};

[[nodiscard]] ResolvedRoots resolve_roots(
    const std::filesystem::path& manifest_or_dir);

struct ManifestVersionRule {
    std::string_view name;
    int number;
    bool rejects_unknown_keys;
};

constexpr ManifestVersionRule manifest_versions[] = {
    {"1.0", 10, false},
    {"1.1", 11, true},
    {"1.2", 12, true},
    {"1.3", 13, true},
};

const ManifestVersionRule* manifest_version(std::string_view version) {
    for (const auto& candidate : manifest_versions)
        if (candidate.name == version) return &candidate;
    return nullptr;
}

std::string supported_manifest_versions() {
    std::string result;
    for (const auto& version : manifest_versions) {
        if (!result.empty()) result += ", ";
        result += version.name;
    }
    return result;
}

std::string_view manifest_version_name(int number) {
    for (const auto& version : manifest_versions)
        if (version.number == number) return version.name;
    return "unknown";
}

}
