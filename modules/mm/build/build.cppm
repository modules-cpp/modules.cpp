module;

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

export module mm.build;

import mm.mdy;

export namespace mm::build {

// Exit codes shared by every tool built on this module. 
inline constexpr int exit_ok       = 0;
inline constexpr int exit_usage    = 64;
inline constexpr int exit_manifest = 65;
inline constexpr int exit_compile  = 80;
inline constexpr int exit_link     = 81;
inline constexpr int exit_run      = 127;

struct Toolchain {
    std::string cxx      = "c++ -fmodules-ts";
    std::string cxxflags = "-std=c++20 -x c++";
    std::string ldflags  = "-std=c++20";
    bool verbose         = false;
};

// Honours $CXX when set.
Toolchain default_toolchain(bool verbose = false);

// One translation unit. A unit that declares a module name is an interface unit
// and produces a BMI under Clang; a unit without one is an implementation unit
// or a plain translation unit and produces only an object. GCC ignores the name.
struct TranslationUnit {
    std::string path;         // root relative
    std::string module_name;  // empty when the unit produces no BMI
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
    std::vector<TranslationUnit> sources;        // root relative
    std::vector<std::string> uses;               // module names
    std::vector<std::filesystem::path> objects;  // filled in by compile
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
    std::string kind;
    std::string name;
    std::size_t parent = static_cast<std::size_t>(-1);  // -1 at the root
    std::vector<std::size_t> children;
};

inline constexpr std::size_t no_parent = static_cast<std::size_t>(-1);

// Every manifest reachable from dir, parents before children, using the same
// cycle-safe rules as load_tree. Sets ok to false and reports the reason on a
// malformed tree.
std::vector<ManifestNode> load_nodes(const std::filesystem::path& dir, bool& ok);

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
// node's kind, or no_target for a kind:project or kind:dir node that builds
// nothing.
struct Project {
    std::vector<ManifestNode> nodes;
    std::vector<mm::mdy::MDYDocument> documents;  // parallel to nodes
    std::vector<std::size_t> target;              // parallel to nodes
    std::vector<BuildableNode> targets;           // kind:module and kind:app
    std::vector<BuildableNode> tests;
    std::vector<BuildableNode> docs;
    bool ok = true;
};

// The one traversal. load_tree and load_nodes are projections of this.
Project load_project(const std::filesystem::path& dir);

// Accepts either a manifest path or the directory holding one.
std::filesystem::path resolve_manifest(std::filesystem::path path);

// Walks up until it finds the mm.mdy declaring kind: project. Empty if none.
std::filesystem::path find_project_root(std::filesystem::path dir);

// Depth first over folder: entries, starting at a kind:project or kind:dir
// manifest. Paths in the result are relative to dir.
Tree load_tree(const std::filesystem::path& dir);

// Loads a single kind:test manifest as a target whose sources are its unit:
// entries. Sets ok to false and reports the reason on failure.
BuildableNode load_test(const std::filesystem::path& manifest_path, bool& ok);

// Topological order over use: edges, dependencies first. False on a cycle or an
// unknown module name.
bool order(const Tree& tree, std::vector<std::size_t>& out);

// The same, restricted to what one target needs: every target reachable from
// index through use:, dependencies first and index itself last. This is how a
// test target builds the modules it uses without building the whole project.
bool order_from(const Tree& tree, std::size_t index, std::vector<std::size_t>& out);

// Objects of a target plus every module reachable through use:, target first.
std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index);

// Quotes a path for /bin/sh. Uses single quotes: $(), backticks and $NAME all
// still expand inside double quotes, so a path is not safe merely for being
// wrapped in them.
std::string shell_quote(const std::filesystem::path& path);

// Runs a command through /bin/sh, returning its exit code rather than a wait
// status. Every path interpolated into the command must go through shell_quote.
int run(const Toolchain& toolchain, const std::string& command);

// Compiles every source of a target, appending to target.objects.
int compile(const Toolchain& toolchain, BuildableNode& target, const std::filesystem::path& build_dir);

// Links objects directly and in order: self registering test suites live in
// static initialisers and an archive would discard them.
int link(const Toolchain& toolchain,
         const std::vector<std::filesystem::path>& objects,
         const std::filesystem::path& output);

// Copies a built binary into bin_dir, unlinking first so a tool can replace the
// binary it is running from.
int install(const std::filesystem::path& from, const std::filesystem::path& bin_dir,
            const std::string& name);

// A stale module interface silently contradicts the sources being compiled.
// False if gcm.cache exists but could not be removed, so a caller can stop
// rather than compile against a cache it failed to actually clear.
[[nodiscard]] bool clear_module_cache();

}
