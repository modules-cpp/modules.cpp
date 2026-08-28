module;

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

export module mm.build;

export namespace mm::build {

// Exit codes shared by every tool built on this module. 
inline constexpr int exit_ok       = 0;
inline constexpr int exit_usage    = 64;
inline constexpr int exit_manifest = 65;
inline constexpr int exit_compile  = 80;
inline constexpr int exit_link     = 81;

struct Toolchain {
    std::string cxx      = "c++ -fmodules-ts";
    std::string cxxflags = "-std=c++20 -x c++";
    std::string ldflags  = "-std=c++20";
    bool verbose         = false;
};

// Honours $CXX when set.
Toolchain default_toolchain(bool verbose = false);

// One buildable thing named by a manifest. Sources are stored root relative and
// in declared order, because declaration order is dependency order.
struct Target {
    std::string kind;                            // module | app | test | doc
    std::string name;
    std::string module_name;                     // kind:module only
    std::filesystem::path dir;                   // root relative
    std::vector<std::string> sources;            // root relative
    std::vector<std::string> uses;               // module names
    std::vector<std::filesystem::path> objects;  // filled in by compile
};

struct Tree {
    std::vector<Target> targets;  // kind:module kind:app kind:doc kind:test
    std::vector<Target> tests;    // kind:test, collected but never built here
    std::vector<Target> docs;     // kind:doc, prose; reached by a walk, never built
    bool ok = true;
};

// Accepts either a manifest path or the directory holding one.
std::filesystem::path resolve_manifest(std::filesystem::path path);

// Walks up until it finds the mm.mdy declaring kind: project. Empty if none.
std::filesystem::path find_project_root(std::filesystem::path dir);

// Depth first over folder: entries, starting at a kind:project or kind:dir
// manifest. Paths in the result are relative to dir.
Tree load_tree(const std::filesystem::path& dir);

// Loads a single kind:test manifest as a target whose sources are its unit:
// entries. Sets ok to false and reports the reason on failure.
Target load_test(const std::filesystem::path& manifest_path, bool& ok);

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
int compile(const Toolchain& toolchain, Target& target, const std::filesystem::path& build_dir);

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
void clear_module_cache();

}
