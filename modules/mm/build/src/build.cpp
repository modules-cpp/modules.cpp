module;

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <sys/wait.h>

module mm.build;

import mm.mdy;

namespace mm::build {

namespace {

const std::vector<std::string>* lookup(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto it = doc.metadata.find(key);
    return it == doc.metadata.end() ? nullptr : &it->second;
}

std::string first(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto* values = lookup(doc, key);
    return values == nullptr || values->empty() ? std::string{} : values->front();
}

std::vector<std::string> all(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto* values = lookup(doc, key);
    return values == nullptr ? std::vector<std::string>{} : *values;
}

// A folder: entry can name any path, including "." or one that climbs out of the
// project or loops back through a symlink. Manifests are therefore identified by
// their canonical path, and the walk keeps two sets: the chain currently being
// visited, which detects cycles, and everything finished, which collapses a
// diamond into one visit instead of duplicating its targets.
struct WalkState {
    std::filesystem::path root;                   // canonical project root
    std::vector<std::filesystem::path> visiting;  // active chain, innermost last
    std::vector<std::filesystem::path> visited;

    bool contains(const std::vector<std::filesystem::path>& set,
                  const std::filesystem::path& path) const {
        for (const auto& entry : set)
            if (entry == path) return true;
        return false;
    }
};

// The result of the guard every traversal has to pass before reading a
// manifest. Both walks share it: the cycle, escape and revisit rules are the
// hardening a review finding demanded, and one implementation is one place to
// get it right.
enum class Enter { ok, skip, error };

Enter enter_manifest(const std::filesystem::path& dir, WalkState& state,
                     std::filesystem::path& manifest, std::filesystem::path& canonical) {
    manifest = (dir / "mm.mdy").lexically_normal();

    if (!std::filesystem::exists(manifest)) {
        std::cerr << "build: missing manifest: " << manifest.string() << "\n";
        return Enter::error;
    }

    // Resolves "..", "." and symlinks, so two spellings of one manifest compare
    // equal and a symlink loop cannot masquerade as a new directory.
    std::error_code ec;
    canonical = std::filesystem::weakly_canonical(manifest, ec);
    if (ec) {
        std::cerr << "build: cannot resolve " << manifest.string() << ": " << ec.message() << "\n";
        return Enter::error;
    }

    const auto relative = canonical.lexically_relative(state.root);
    if (relative.empty() || *relative.begin() == "..") {
        std::cerr << "build: manifest outside the project root: " << canonical.string() << "\n";
        return Enter::error;
    }

    if (state.contains(state.visiting, canonical)) {
        std::cerr << "build: folder: cycle in the manifest tree:\n";
        for (const auto& entry : state.visiting)
            std::cerr << "    " << entry.lexically_relative(state.root).string() << "\n";
        std::cerr << "    " << relative.string() << "  <- repeats\n";
        return Enter::error;
    }

    if (state.contains(state.visited, canonical)) return Enter::skip;

    return Enter::ok;
}

// The kind and name every manifest must declare to be usable further,
// independent of whether the walk that reached it builds a Target (walk) or
// only records a Node (load_nodes' walk_nodes). Both walks share it: without
// this, load_nodes recorded whatever a manifest's front matter said, kind
// included, with no check that it named one of the six kinds this project
// defines at all.
bool valid_manifest(std::string_view kind, std::string_view name,
                    const std::filesystem::path& manifest) {
    if (kind != "project" && kind != "dir" && kind != "module" &&
        kind != "app" && kind != "test" && kind != "doc") {
        std::cerr << "build: unknown kind \"" << kind << "\" in " << manifest.string() << "\n";
        return false;
    }
    if (name.empty()) {
        std::cerr << "build: manifest has no name: " << manifest.string() << "\n";
        return false;
    }
    return true;
}

void walk(const std::filesystem::path& dir, Tree& tree, WalkState& state) {
    std::filesystem::path manifest;
    std::filesystem::path canonical;

    switch (enter_manifest(dir, state, manifest, canonical)) {
        case Enter::error: tree.ok = false; return;
        case Enter::skip:  return;
        case Enter::ok:    break;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest);
    const auto kind = first(doc, "kind");
    const auto name = first(doc, "name");

    if (!valid_manifest(kind, name, manifest)) {
        tree.ok = false;
        state.visited.push_back(canonical);
        return;
    }

    if (kind == "project" || kind == "dir") {
        state.visiting.push_back(canonical);
        for (const auto& folder : all(doc, "folder")) walk(dir / folder, tree, state);
        state.visiting.pop_back();
        state.visited.push_back(canonical);
        return;
    }

    state.visited.push_back(canonical);

    Target target;
    target.kind = kind;
    target.name = name;
    target.module_name = first(doc, "module");
    target.dir = dir.lexically_normal();
    target.uses = all(doc, "use");

    if (kind == "doc") {
        // Prose. Listed so a walk sees it, but nothing compiles or links it, so
        // an empty file: list is not an error.
        for (const auto& file : all(doc, "file")) {
            auto unit = parse_unit(file);
            unit.path = (dir / unit.path).lexically_normal().string();
            target.sources.push_back(std::move(unit));
        }

        tree.docs.push_back(std::move(target));
        return;
    }

    if (kind == "test") {
        // unit: entries are already root relative.
        for (const auto& unit : all(doc, "unit")) target.sources.push_back(parse_unit(unit));

        if (target.sources.empty()) {
            std::cerr << "build: manifest declares no unit: entries: " << manifest.string() << "\n";
            tree.ok = false;
            return;
        }

        tree.tests.push_back(std::move(target));
        return;
    }

    // file: entries are relative to the manifest.
    for (const auto& file : all(doc, "file")) {
        auto unit = parse_unit(file);
        unit.path = (dir / unit.path).lexically_normal().string();
        target.sources.push_back(std::move(unit));
    }

    if (target.sources.empty()) {
        std::cerr << "build: manifest declares no file: entries: " << manifest.string() << "\n";
        tree.ok = false;
        return;
    }
    if (kind == "module" && target.module_name.empty()) {
        std::cerr << "build: module manifest has no module: name: " << manifest.string() << "\n";
        tree.ok = false;
        return;
    }

    tree.targets.push_back(std::move(target));
}

void walk_nodes(const std::filesystem::path& dir, std::size_t parent,
                std::vector<Node>& nodes, WalkState& state, bool& ok) {
    std::filesystem::path manifest;
    std::filesystem::path canonical;

    switch (enter_manifest(dir, state, manifest, canonical)) {
        case Enter::error: ok = false; return;
        case Enter::skip:  return;
        case Enter::ok:    break;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest);
    const auto kind = first(doc, "kind");
    const auto name = first(doc, "name");

    if (!valid_manifest(kind, name, manifest)) {
        ok = false;
        state.visited.push_back(canonical);
        return;
    }

    Node node;
    node.manifest = manifest;
    node.dir = dir.lexically_normal();
    node.kind = kind;
    node.name = name;
    node.parent = parent;

    nodes.push_back(std::move(node));
    const auto index = nodes.size() - 1;

    if (parent != no_parent) nodes[parent].children.push_back(index);

    if (nodes[index].kind == "project" || nodes[index].kind == "dir") {
        state.visiting.push_back(canonical);
        for (const auto& folder : all(doc, "folder"))
            walk_nodes(dir / folder, index, nodes, state, ok);
        state.visiting.pop_back();
    }

    state.visited.push_back(canonical);
}

std::size_t index_of_module(const Tree& tree, const std::string& module_name) {
    for (std::size_t i = 0; i < tree.targets.size(); ++i)
        if (tree.targets[i].kind == "module" && tree.targets[i].module_name == module_name)
            return i;
    return tree.targets.size();
}

bool order_visit(std::size_t index, const Tree& tree,
                 std::vector<int>& state, std::vector<std::size_t>& out) {
    if (state[index] == 2) return true;
    if (state[index] == 1) {
        std::cerr << "build: dependency cycle through " << tree.targets[index].name << "\n";
        return false;
    }

    state[index] = 1;

    for (const auto& used : tree.targets[index].uses) {
        const auto dependency = index_of_module(tree, used);
        if (dependency == tree.targets.size()) {
            std::cerr << "build: " << tree.targets[index].name
                      << " uses unknown module " << used << "\n";
            return false;
        }
        if (!order_visit(dependency, tree, state, out)) return false;
    }

    state[index] = 2;
    out.push_back(index);
    return true;
}

void closure_visit(std::size_t index, const Tree& tree,
                   std::vector<bool>& seen, std::vector<std::filesystem::path>& out) {
    if (seen[index]) return;
    seen[index] = true;

    for (const auto& object : tree.targets[index].objects) out.push_back(object);

    for (const auto& used : tree.targets[index].uses) {
        const auto dependency = index_of_module(tree, used);
        if (dependency != tree.targets.size()) closure_visit(dependency, tree, seen, out);
    }
}

}

Toolchain default_toolchain(bool verbose) {
    Toolchain toolchain;
    if (const char* env = std::getenv("CXX"); env != nullptr)
        toolchain.cxx = std::string(env) + " -fmodules-ts";
    toolchain.verbose = verbose;
    return toolchain;
}

std::string bmi_name(std::string_view module_name) {
    std::string name;
    name.reserve(module_name.size() + 4);

    for (const char c : module_name) name += (c == ':' ? '-' : c);
    name += ".pcm";

    return name;
}

Unit parse_unit(std::string_view value) {
    Unit unit;

    const auto split = value.find_first_of(" \t");
    if (split == std::string_view::npos) {
        unit.path = std::string(value);
        return unit;
    }

    unit.path = std::string(value.substr(0, split));

    const auto name = value.find_first_not_of(" \t", split);
    if (name != std::string_view::npos) unit.module_name = std::string(value.substr(name));

    return unit;
}

std::filesystem::path resolve_manifest(std::filesystem::path path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) path /= "mm.mdy";
    return path;
}

std::filesystem::path find_project_root(std::filesystem::path dir) {
    for (; !dir.empty(); dir = dir.parent_path()) {
        const auto candidate = dir / "mm.mdy";
        if (std::filesystem::exists(candidate) &&
            first(mm::mdy::Parser::parse_file(candidate), "kind") == "project") {
            return dir;
        }
        if (!dir.has_relative_path()) break;
    }
    return {};
}

Tree load_tree(const std::filesystem::path& dir) {
    Tree tree;

    std::error_code ec;
    WalkState state;
    state.root = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        std::cerr << "build: cannot resolve project root " << dir.string()
                  << ": " << ec.message() << "\n";
        tree.ok = false;
        return tree;
    }

    walk(dir, tree, state);
    if (!tree.ok) return tree;

    // Checked once the whole tree is built, not incrementally during the
    // walk, since a duplicate can only be found once every candidate name
    // is known. Without this, index_of_module returns whichever target
    // with a given module: name it happens to see first, silently treating
    // two real, different modules as interchangeable; two app: targets with
    // the same name would silently install() one over the other under
    // out/bin; and nothing at all currently rules out two different
    // manifests claiming the same directory.
    std::map<std::string, const Target*, std::less<>> modules_by_name;
    std::map<std::string, const Target*, std::less<>> apps_by_name;
    std::map<std::filesystem::path, const Target*> targets_by_dir;

    auto check_dir = [&](const Target& target) {
        const auto it = targets_by_dir.find(target.dir);
        if (it != targets_by_dir.end()) {
            std::cerr << "build: " << target.dir.string() << " is declared by more than one manifest: "
                      << it->second->name << " and " << target.name << "\n";
            tree.ok = false;
            return;
        }
        targets_by_dir[target.dir] = &target;
    };

    for (const auto& target : tree.targets) {
        check_dir(target);

        if (target.kind == "module") {
            const auto it = modules_by_name.find(target.module_name);
            if (it != modules_by_name.end()) {
                std::cerr << "build: module: " << target.module_name << " is exported by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                tree.ok = false;
            } else {
                modules_by_name[target.module_name] = &target;
            }
        } else if (target.kind == "app") {
            const auto it = apps_by_name.find(target.name);
            if (it != apps_by_name.end()) {
                std::cerr << "build: app name \"" << target.name << "\" is declared by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                tree.ok = false;
            } else {
                apps_by_name[target.name] = &target;
            }
        }
    }

    for (const auto& target : tree.tests) check_dir(target);
    for (const auto& target : tree.docs) check_dir(target);

    return tree;
}

std::vector<Node> load_nodes(const std::filesystem::path& dir, bool& ok) {
    std::vector<Node> nodes;
    ok = true;

    std::error_code ec;
    WalkState state;
    state.root = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        std::cerr << "build: cannot resolve project root " << dir.string()
                  << ": " << ec.message() << "\n";
        ok = false;
        return nodes;
    }

    walk_nodes(dir, no_parent, nodes, state, ok);
    return nodes;
}

Target load_test(const std::filesystem::path& manifest_path, bool& ok) {
    ok = false;
    Target target;

    if (!std::filesystem::exists(manifest_path)) {
        std::cerr << "build: manifest does not exist: " << manifest_path.string() << "\n";
        return target;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest_path);

    const auto kind = first(doc, "kind");
    if (kind != "test") {
        std::cerr << "build: manifest kind is \"" << kind << "\", expected \"test\"\n";
        return target;
    }

    target.kind = kind;
    target.name = first(doc, "name");
    target.module_name = first(doc, "module");
    target.dir = manifest_path.parent_path();
    target.uses = all(doc, "use");
    for (const auto& unit : all(doc, "unit")) target.sources.push_back(parse_unit(unit));

    if (target.name.empty()) {
        std::cerr << "build: manifest has no name\n";
        return target;
    }
    if (target.sources.empty()) {
        std::cerr << "build: manifest declares no unit: entries\n";
        return target;
    }

    ok = true;
    return target;
}

bool order(const Tree& tree, std::vector<std::size_t>& out) {
    std::vector<int> state(tree.targets.size(), 0);
    out.clear();
    out.reserve(tree.targets.size());

    for (std::size_t i = 0; i < tree.targets.size(); ++i)
        if (!order_visit(i, tree, state, out)) return false;

    return true;
}

bool order_from(const Tree& tree, std::size_t index, std::vector<std::size_t>& out) {
    std::vector<int> state(tree.targets.size(), 0);
    out.clear();

    return order_visit(index, tree, state, out);
}

std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index) {
    std::vector<bool> seen(tree.targets.size(), false);
    std::vector<std::filesystem::path> objects;
    closure_visit(index, tree, seen, objects);
    return objects;
}

int run(const Toolchain& toolchain, const std::string& command) {
    if (toolchain.verbose) std::cout << "    " << command << "\n";

    // The child writes straight to the terminal; without this our own buffered
    // output would appear after it when stdout is a pipe.
    std::cout.flush();

    const int status = std::system(command.c_str());
    if (status == -1) return -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

// Single quotes disable every form of shell expansion, and the only character
// that cannot appear between them is the single quote itself, which is handled
// by closing the run, emitting an escaped quote and reopening. Double quotes
// would not do: $(), `` and $NAME all still expand inside them, so a path such
// as "$(touch x).cppm" would execute rather than name a file.
std::string shell_quote(const std::filesystem::path& path) {
    const std::string& text = path.native();

    std::string quoted;
    quoted.reserve(text.size() + 2);

    quoted += '\'';
    for (const char c : text) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';

    return quoted;
}

int compile(const Toolchain& toolchain, Target& target, const std::filesystem::path& build_dir) {
    std::error_code ec;

    for (const auto& source : target.sources) {
        if (!std::filesystem::exists(source.path)) {
            std::cerr << "build: source does not exist: " << source.path << "\n";
            return exit_manifest;
        }

        const auto object = build_dir / (source.path + ".o");
        std::filesystem::create_directories(object.parent_path(), ec);

        std::cout << "    " << source.path << "\n";

        const auto command = toolchain.cxx + " " + toolchain.cxxflags +
                             " -c " + shell_quote(source.path) + " -o " + shell_quote(object);
        if (run(toolchain, command) != 0) {
            std::cerr << "build: failed to compile " << source.path << "\n";
            return exit_compile;
        }

        target.objects.push_back(object);
    }

    return exit_ok;
}

int link(const Toolchain& toolchain,
         const std::vector<std::filesystem::path>& objects,
         const std::filesystem::path& output) {
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);

    // Unlink before writing: a tool can be rebuilding the very binary it is
    // running from, and overwriting a running executable fails with ETXTBSY.
    std::filesystem::remove(output, ec);

    std::string command = toolchain.cxx + " " + toolchain.ldflags;
    for (const auto& object : objects) command += " " + shell_quote(object);
    command += " -o " + shell_quote(output);

    if (run(toolchain, command) != 0) {
        std::cerr << "build: failed to link " << output.string() << "\n";
        return exit_link;
    }

    return exit_ok;
}

int install(const std::filesystem::path& from, const std::filesystem::path& bin_dir,
            const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(bin_dir, ec);

    const auto installed = bin_dir / name;
    std::filesystem::remove(installed, ec);
    std::filesystem::copy_file(from, installed, ec);

    if (ec) {
        std::cerr << "build: failed to install " << name << ": " << ec.message() << "\n";
        return exit_link;
    }

    return exit_ok;
}

void clear_module_cache() {
    std::error_code ec;
    std::filesystem::remove_all("gcm.cache", ec);
}

}
