module;

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

export module mm.build;

import mm.mdy;
import mm.configure;

export namespace mm::build {

// Exit codes shared by every tool built on this module. 
inline constexpr int exit_ok       = 0;
inline constexpr int exit_usage    = 64;
inline constexpr int exit_manifest = 65;
inline constexpr int exit_compile  = 80;
inline constexpr int exit_link     = 81;
inline constexpr int exit_run      = 127;

using CompilerFamily = mm::configure::CompilerFamily;
using Build = mm::configure::Build;

struct ToolchainProgram {
    std::string invocation;
    std::string arguments;
};

struct Toolchain {
    CompilerFamily family = CompilerFamily::Gcc;
    std::string target = "host";
    ToolchainProgram compiler = {
        "g++", std::string(mm::configure::build_compile_flags(mm::configure::Build::Debug))};
    ToolchainProgram assembler = {"g++", {}};
    ToolchainProgram linker = {
        "g++", std::string(mm::configure::build_link_flags(mm::configure::Build::Debug))};
    ToolchainProgram librarian;
    ToolchainProgram debugger;
    bool verbose = false;
};

using mm::configure::compiler_family_name;
using mm::configure::build_name;

// The unconfigured project default: a debug build with GCC. Compiler and build
// selection are persisted by configure rather than chosen by each process.
Toolchain default_toolchain(bool verbose = false);

// The host lane and any cross record retained from out/config.mdy. Selection
// is private so the object cannot select a missing cross lane; current build
// front ends execute selected_toolchain(). Malformed or unreadable
// configuration returns false, while callers separately decide whether an
// absent file means fallback.
class BuildConfiguration {
public:
    Build build = Build::Debug;
    std::filesystem::path build_directory;        // the selected lane's output
    std::filesystem::path host_build_directory;

    [[nodiscard]] const Toolchain& host_toolchain() const { return host_; }
    [[nodiscard]] const Toolchain* cross_toolchain() const {
        return cross_ ? &*cross_ : nullptr;
    }
    [[nodiscard]] const std::filesystem::path* cross_build_directory() const {
        return cross_build_directory_ ? &*cross_build_directory_ : nullptr;
    }
    [[nodiscard]] bool selects_cross() const { return selects_cross_; }

    [[nodiscard]] const Toolchain* toolchain_for(bool target) const {
        if (target) return cross_toolchain();
        return &host_;
    }
    [[nodiscard]] const std::filesystem::path* build_directory_for(bool target) const {
        if (target) return cross_build_directory();
        return &host_build_directory;
    }

    // The loader is the only writer of the lane selection, so selects_cross_
    // can never be true without a cross_ value for these accessors to return.
    [[nodiscard]] const Toolchain& selected_toolchain() const {
        return selects_cross_ ? cross_.value() : host_;
    }
    [[nodiscard]] Toolchain& selected_toolchain() {
        return selects_cross_ ? cross_.value() : host_;
    }

private:
    Toolchain host_;
    std::optional<Toolchain> cross_;
    std::optional<std::filesystem::path> cross_build_directory_;
    bool selects_cross_ = false;

    friend bool load_configuration(const std::filesystem::path&, bool, BuildConfiguration&);
    friend bool resolve_configuration(const std::filesystem::path&, bool, BuildConfiguration&);
};

[[nodiscard]] bool load_configuration(const std::filesystem::path& path,
                                      bool verbose,
                                      BuildConfiguration& configuration);

// Loads project_root/out/config.mdy when present; otherwise returns the shared
// debug GCC default and the legacy out build directory. Every compiling front
// end uses this resolver so build and test cannot choose different values.
[[nodiscard]] bool resolve_configuration(const std::filesystem::path& project_root,
                                         bool verbose,
                                         BuildConfiguration& configuration);

// One translation unit. An explicit module name identifies an importable unit
// and its Clang BMI; the primary .cppm of a kind:module target may instead
// inherit BuildableNode::module_name. GCC ignores this per-unit name.
struct TranslationUnit {
    std::string path;         // root relative
    std::string module_name;  // optional explicit Clang BMI/module name
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

// Caller-specific diagnostics; only configure opts into a strict tree.
struct LoadPolicy {
    std::string_view tool = "build";
    bool strict_tree = false;
    bool warn_options = false;
};

[[nodiscard]] bool validate_manifest_schema(const mm::mdy::MDYDocument& document,
                                            const std::filesystem::path& manifest,
                                            const LoadPolicy& policy = {});

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

// Resolved build capability for every Project::nodes entry. The selected lane
// chooses one of these vectors; keeping both preserves the declaration for
// diagnostics and model consumers without resolving the tree twice.
struct BuildCapabilities {
    std::vector<bool> host;
    std::vector<bool> target;
};

// The one traversal. load_tree and load_nodes are projections of this.
Project load_project(const std::filesystem::path& dir, const LoadPolicy& policy = {});

// Shared data adapter; no parsing, validation, or option resolution here.
std::vector<mm::configure::OptionNode> configuration_nodes(const Project& project);

// Capability is declared per node but constrains dependencies: building a node
// for a lane means building every module it reaches through use: for that lane
// too. Checks cap(consumer) is a subset of cap(dependency) for both lanes over
// the resolved buildable-host and buildable-target values, which the folder-tree
// resolver cannot see because it never follows a use: edge. Reports each
// violation with both manifests and the offending value's origin.
[[nodiscard]] bool validate_capabilities(const std::vector<mm::configure::OptionNode>& nodes,
                                         const std::vector<mm::configure::OptionValues>& resolved,
                                         std::string_view tool = "configure");

// Resolve only the two capability options consumed by build and test. Other
// options retain their release-1.1 warning-only behavior in those tools.
[[nodiscard]] bool resolve_capabilities(const std::filesystem::path& project_root, Build build,
                                        const Project& project, BuildCapabilities& capabilities,
                                        std::string_view tool);

// Accepts either a manifest path or the directory holding one.
std::filesystem::path resolve_manifest(std::filesystem::path path);

// Walks up until it finds the mm.mdy declaring kind: project. Empty if none.
std::filesystem::path find_project_root(std::filesystem::path dir);

// Depth first over folder: entries, starting at a kind:project or kind:dir
// manifest. Paths in the result are relative to dir.
Tree load_tree(const std::filesystem::path& dir, const LoadPolicy& policy = {});

// Loads a single kind:test manifest as a target whose sources are its unit:
// entries. Sets ok to false and reports the reason on failure.
BuildableNode load_test(const std::filesystem::path& manifest_path, bool& ok,
                        const LoadPolicy& policy = {});

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
// Clears both compiler families' module artifacts for this output lane and
// returns false if either cannot be removed. Object paths intentionally do not
// vary by compiler, and every build recompiles them.
[[nodiscard]] bool clear_module_cache(const std::filesystem::path& build_dir = "out");

}
