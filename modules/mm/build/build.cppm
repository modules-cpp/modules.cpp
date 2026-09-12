module;

#include <cstddef>
#include <filesystem>
#include <map>
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
inline constexpr int exit_unavailable = 77;
inline constexpr int exit_compile  = 80;
inline constexpr int exit_link     = 81;
inline constexpr int exit_run      = 127;

using CompilerFamily = mm::configure::CompilerFamily;
using Build = mm::configure::Build;

struct ToolchainProgram {
    std::string invocation;
    std::string arguments;
};

using RunnerImage = mm::configure::RunnerImage;

struct ToolchainRunner {
    std::string invocation;
    std::vector<std::string> prefix_arguments;
    RunnerImage image = RunnerImage::Positional;
    std::string image_option;
    std::vector<std::string> image_arguments;
    std::vector<std::string> suffix_arguments;
    bool forwards_arguments = true;
};

using DebuggerConnection = mm::configure::DebuggerConnection;

struct ToolchainDebugger {
    std::string invocation;
    std::vector<std::string> prefix_arguments;
    DebuggerConnection connection = DebuggerConnection::Direct;
    std::string remote_endpoint;
    std::vector<std::string> runner_arguments;
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
    ToolchainProgram c_compiler;
    std::optional<ToolchainDebugger> debugger;
    std::optional<ToolchainRunner> runner;
    bool verbose = false;
};

using Platform = mm::configure::PlatformSettings;

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
    [[nodiscard]] bool target_has_host_capability() const {
        return target_has_host_capability_;
    }
    [[nodiscard]] const Platform& host_platform() const { return host_platform_; }
    [[nodiscard]] const Platform* target_platform() const {
        return selects_cross_ ? configured_target_platform() : nullptr;
    }
    [[nodiscard]] const Platform* configured_target_platform() const {
        return cross_platform_ ? &*cross_platform_ : nullptr;
    }
    [[nodiscard]] bool configuration_2() const { return configuration_2_; }

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
    Platform host_platform_;
    std::optional<Platform> cross_platform_;
    bool selects_cross_ = false;
    bool target_has_host_capability_ = false;
    bool configuration_2_ = false;

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
    std::string requires_board;                  // kind:app or kind:test only
    std::string library;                         // kind:module only
    std::vector<std::filesystem::path> objects;  // filled in by compile
};

struct SdkDefinition {
    std::size_t node = static_cast<std::size_t>(-1);
    std::string name;
    std::filesystem::path manifest;
    std::string target;
    CompilerFamily family = CompilerFamily::Gcc;
    mm::configure::PlatformRuntime runtime = mm::configure::PlatformRuntime::Unknown;
    std::string specs_profile;
    std::filesystem::path specs_file;
    std::optional<std::filesystem::path> sysroot;
    std::optional<std::filesystem::path> runtime_prefix;
    std::vector<mm::configure::Responsibility> provides;
    std::string library;
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
    std::string machine;
    std::filesystem::path linker_script;
    std::vector<std::filesystem::path> sources;
    std::vector<mm::configure::Responsibility> provides;
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

struct Availability {
    bool available = false;
    std::string reason;
};

[[nodiscard]] Availability availability(const Project& project, std::size_t node,
                                        bool capability, bool target_lane,
                                        const Platform* platform);

[[nodiscard]] bool can_link_executable(const Platform* platform, std::string_view tool,
                                       std::string_view name);

[[nodiscard]] bool check_configuration_staleness(const BuildConfiguration& configuration,
                                                 const Project& project,
                                                 bool target_lane,
                                                 std::string_view tool = "build");

[[nodiscard]] std::optional<BuildableNode> platform_unit(const Platform* platform);

// One resolved structural property, including the manifest that assigned or
// reset it and the origin of a read-only lock. These facts are retained for
// diagnostics rather than flattened into booleans at the resolver boundary.
struct StructuralProperty {
    bool value = true;
    mm::configure::OptionOrigin origin = mm::configure::OptionOrigin::Default;
    std::filesystem::path value_source;
    bool read_only = false;
    std::filesystem::path lock_source;
};

struct NodeStructuralProperties {
    StructuralProperty buildable_host;
    StructuralProperty buildable_target;
    StructuralProperty core;
};

// Resolved structural properties for every Project::nodes entry. Host and
// target are lane capabilities; core is an edge constraint and is never a
// third lane.
struct StructuralProperties {
    std::vector<NodeStructuralProperties> nodes;

    // A hosted target can produce nodes admitted by either capability. The
    // union changes only target artifact selection, never which tools the
    // current build invokes from out/bin.
    [[nodiscard]] std::vector<bool> lane(bool target_lane,
                                         bool target_has_host_capability) const;
};

[[nodiscard]] StructuralProperties structural_properties(
    const std::vector<mm::configure::OptionValues>& resolved);

// The one traversal. load_tree and load_nodes are projections of this.
Project load_project(const std::filesystem::path& dir, const LoadPolicy& policy = {});

// Shared data adapter; no parsing, validation, or option resolution here.
std::vector<mm::configure::OptionNode> configuration_nodes(const Project& project);

// Structural properties constrain use: edges. A dependency must support every
// lane its consumer supports, and a core consumer may not use a non-core
// module. Direct-edge checks imply the same rules over the transitive closure.
[[nodiscard]] bool validate_structural_properties(
    const std::vector<mm::configure::OptionNode>& nodes,
    const StructuralProperties& properties,
    std::string_view tool = "configure");

// Resolve only the structural properties consumed by lane tools. Other
// options retain their warning-only behavior in those tools.
[[nodiscard]] bool resolve_structural_properties(
    const std::filesystem::path& project_root, Build build,
    const Project& project, StructuralProperties& properties,
    std::string_view tool);

// Re-observes the working tree and validates the selected SDK's library
// checkout. No presence result is persisted in a configuration record.
[[nodiscard]] bool validate_library_checkout(const std::filesystem::path& project_root,
                                             const LibraryDefinition& library,
                                             std::string_view tool = "configure");

// Resolves the include interface of a module's library at the point where the
// module is actually compiled. This is deliberately demand-driven: an absent
// checkout remains valid until a selected build or test closure reaches its
// wrapper module. Returned paths are absolute, resolved from project_root, so
// compile does not depend on the caller's current working directory.
[[nodiscard]] bool library_include_directories(
    const std::filesystem::path& project_root,
    const std::vector<LibraryDefinition>& libraries,
    const BuildableNode& target,
    std::vector<std::filesystem::path>& directories,
    std::string_view tool = "build");

// Accepts either a manifest path or the directory holding one.
std::filesystem::path resolve_manifest(std::filesystem::path path);

// Walks up until it finds the mm.mdy declaring kind: project. Empty if none.
std::filesystem::path find_project_root(std::filesystem::path dir);

// Depth first over folder: entries, starting at a kind:project, kind:dir, or kind:library
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

// Compiles every source of a target, appending to target.objects. Library
// include directories are separate from the configured compiler argument
// string so each path remains one shell-quoted argument.
int compile(const Toolchain& toolchain, BuildableNode& target,
            const std::filesystem::path& build_dir,
            const std::vector<std::filesystem::path>& include_directories = {});

// Links objects directly and in order: self registering test suites live in
// static initialisers and an archive would discard them.
int link(const Toolchain& toolchain,
         const std::vector<std::filesystem::path>& objects,
         const std::filesystem::path& output);

// Copies a built binary into bin_dir, unlinking first so a tool can replace the
// binary it is running from.
int install(const std::filesystem::path& from, const std::filesystem::path& bin_dir,
            const std::string& name);

// Formats a value as a CMake bracket argument, selecting an equals count up to
// max_equals that does not appear in the payload. Returns nullopt if value
// contains a newline, carriage return, or semicolon, or exceeds the bounded length.
[[nodiscard]] std::optional<std::string> cmake_bracket_argument(std::string_view value,
                                                                std::size_t max_equals = 10);

// Generates mm-toolchain.cmake for external CMake builds. Carries compiler identity,
// sysroot, try-compile static library, and CMAKE_SYSTEM_NAME (Linux for hosted lanes,
// Generic for bare-metal), and carries no processor flags.
[[nodiscard]] bool write_toolchain_cmake(const std::filesystem::path& destination,
                                         const Toolchain& toolchain,
                                         const Platform& platform);

// Generates mm-inputs.cmake for external CMake builds.
[[nodiscard]] bool write_inputs_cmake(
    const std::filesystem::path& destination,
    const std::vector<std::filesystem::path>& objects,
    const std::string& output_name,
    const std::filesystem::path& library_source,
    const std::string& board_name);

struct ProjectionSchema {
    std::string_view target_triple;
    std::vector<std::string_view> fields;
};

[[nodiscard]] const ProjectionSchema* find_projection_schema(std::string_view target_triple);

[[nodiscard]] bool query_driver_projection(
    const std::string& c_driver,
    const std::vector<std::string>& sanitised_options,
    const ProjectionSchema& schema,
    std::map<std::string, std::string>& projection,
    std::string_view tool);

// Parses one LC_ALL=C GCC --help=target listing against a closed schema.
// Value fields may be present with an empty value: GCC uses that representation
// when an equivalent architecture spelling leaves a CPU name unset.
[[nodiscard]] bool parse_driver_projection(
    std::string_view output,
    const ProjectionSchema& schema,
    std::map<std::string, std::string>& projection,
    std::string_view tool);

[[nodiscard]] bool publish_external_results(
    const std::filesystem::path& results_file,
    const std::filesystem::path& external_dir,
    const std::string& output_name,
    const std::filesystem::path& target_output,
    std::string_view tool);

[[nodiscard]] int external_link(
    const Project& project,
    const Platform& platform,
    const Toolchain& toolchain,
    const std::string& app_name,
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& build_dir,
    const std::filesystem::path& target_output,
    bool verbose = false);

// A stale module interface silently contradicts the sources being compiled.
// Clears both compiler families' module artifacts for this output lane and
// returns false if either cannot be removed. Object paths intentionally do not
// vary by compiler, and every build recompiles them.
[[nodiscard]] bool clear_module_cache(const std::filesystem::path& build_dir = "out");

}
