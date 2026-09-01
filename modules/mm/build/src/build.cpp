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

bool safe_exists(const std::filesystem::path& path) {
    std::error_code ec;
    const bool found = std::filesystem::exists(path, ec);
    if (ec) std::cerr << "build: cannot check " << path.string() << ": " << ec.message() << "\n";
    return found;
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

    if (!safe_exists(manifest)) {
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
bool is_safe_name(std::string_view name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string_view::npos;
}

bool is_safe_relative_path(const std::filesystem::path& raw, const std::filesystem::path& joined) {
    if (raw.is_absolute()) return false;
    for (const auto& part : joined.lexically_normal())
        if (part == "..") return false;
    return true;
}

// weakly_canonical resolves symlinks in whatever prefix of path already
// exists (a real fix for a symlink planted under out/ that would otherwise
// redirect a write outside the project), but when nothing on path exists
// at all it returns path unchanged and still relative rather than an
// absolute fallback, the bug fixed earlier by switching to absolute() +
// lexically_normal() alone. That case is safe to fall back to lexical
// resolution: a path cannot be escaped through a symlink that does not
// exist yet.
bool within_root(const std::filesystem::path& path) {
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec);
    if (ec) return false;

    auto resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec) return false;
    if (!resolved.is_absolute())
        resolved = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return false;

    const auto relative = resolved.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

// The one manifest format version this project understands. Every real
// manifest declares mm: 1.0; nothing else is defined yet.
constexpr std::string_view supported_mm_version = "1.0";

bool valid_mm_version(const mm::mdy::MDYDocument& doc, const std::filesystem::path& manifest) {
    const auto* values = lookup(doc, "mm");
    if (values == nullptr || values->empty()) {
        std::cerr << "build: manifest has no mm: version: " << manifest.string() << "\n";
        return false;
    }
    if (values->size() > 1) {
        std::cerr << "build: manifest declares mm: more than once: " << manifest.string() << "\n";
        return false;
    }
    if (values->front() != supported_mm_version) {
        std::cerr << "build: unsupported mm: version \"" << values->front() << "\" in "
                  << manifest.string() << " (supported: " << supported_mm_version << ")\n";
        return false;
    }
    return true;
}

bool valid_manifest(const mm::mdy::MDYDocument& doc, std::string_view kind, std::string_view name,
                    const std::filesystem::path& manifest) {
    if (!valid_mm_version(doc, manifest)) return false;
    if (kind != "project" && kind != "dir" && kind != "module" &&
        kind != "app" && kind != "test" && kind != "doc") {
        std::cerr << "build: unknown kind \"" << kind << "\" in " << manifest.string() << "\n";
        return false;
    }
    if (name.empty()) {
        std::cerr << "build: manifest has no name: " << manifest.string() << "\n";
        return false;
    }
    if (!is_safe_name(name)) {
        std::cerr << "build: unsafe name \"" << name << "\" in " << manifest.string() << "\n";
        return false;
    }
    return true;
}

// The one traversal: the structural node, the parsed document, and the
// target a manifest declares, all recorded from a single read of that
// manifest. walk and walk_nodes were separate recursive walkers doing the
// first two halves of this independently, which meant every caller wanting
// both read and parsed each manifest twice, with no guarantee the two reads
// saw the same bytes.
void walk_project(const std::filesystem::path& dir, std::size_t parent, Project& project,
                  WalkState& state) {
    std::filesystem::path manifest;
    std::filesystem::path canonical;

    switch (enter_manifest(dir, state, manifest, canonical)) {
        case Enter::error: project.ok = false; return;
        case Enter::skip:  return;
        case Enter::ok:    break;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest);
    const auto kind = first(doc, "kind");
    const auto name = first(doc, "name");

    if (!valid_manifest(doc, kind, name, manifest)) {
        project.ok = false;
        state.visited.push_back(canonical);
        return;
    }

    Node node;
    node.manifest = manifest;
    node.dir = dir.lexically_normal();
    node.kind = kind;
    node.name = name;
    node.parent = parent;

    project.nodes.push_back(std::move(node));
    const auto index = project.nodes.size() - 1;

    // documents and target stay parallel to nodes, so every push here is
    // matched by one in each.
    project.documents.push_back(doc);
    project.target.push_back(no_target);

    if (parent != no_parent) project.nodes[parent].children.push_back(index);

    if (kind == "project" || kind == "dir") {
        state.visiting.push_back(canonical);
        for (const auto& folder : all(doc, "folder"))
            walk_project(dir / folder, index, project, state);
        state.visiting.pop_back();
        state.visited.push_back(canonical);
        return;
    }

    // A leaf manifest is finished the moment it is read, and must be marked
    // so before its target is built: a diamond reaches the same folder from
    // two parents, and only this stops the second visit building a second,
    // duplicate target for it.
    state.visited.push_back(canonical);

    Target target;
    target.kind = kind;
    target.name = name;
    target.module_name = first(doc, "module");
    target.dir = dir.lexically_normal();
    target.uses = all(doc, "use");

    const auto push_source = [&](std::string_view value, bool join_with_dir) {
        auto unit = parse_unit(value);
        const std::filesystem::path raw = unit.path;
        const std::filesystem::path joined = join_with_dir ? dir / raw : raw;
        if (!is_safe_relative_path(raw, joined)) {
            std::cerr << "build: unsafe source path \"" << unit.path << "\" in "
                      << manifest.string() << "\n";
            project.ok = false;
            return false;
        }
        unit.path = joined.lexically_normal().string();
        target.sources.push_back(std::move(unit));
        return true;
    };

    if (kind == "doc") {
        // Prose. Listed so a walk sees it, but nothing compiles or links it, so
        // an empty file: list is not an error.
        for (const auto& file : all(doc, "file"))
            if (!push_source(file, true)) return;

        project.docs.push_back(std::move(target));
        project.target[index] = project.docs.size() - 1;
        return;
    }

    if (kind == "test") {
        // unit: entries are already root relative.
        for (const auto& unit : all(doc, "unit"))
            if (!push_source(unit, false)) return;

        if (target.sources.empty()) {
            std::cerr << "build: manifest declares no unit: entries: " << manifest.string() << "\n";
            project.ok = false;
            return;
        }

        project.tests.push_back(std::move(target));
        project.target[index] = project.tests.size() - 1;
        return;
    }

    // file: entries are relative to the manifest.
    for (const auto& file : all(doc, "file"))
        if (!push_source(file, true)) return;

    if (target.sources.empty()) {
        std::cerr << "build: manifest declares no file: entries: " << manifest.string() << "\n";
        project.ok = false;
        return;
    }
    if (kind == "module" && target.module_name.empty()) {
        std::cerr << "build: module manifest has no module: name: " << manifest.string() << "\n";
        project.ok = false;
        return;
    }

    project.targets.push_back(std::move(target));
    project.target[index] = project.targets.size() - 1;
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
        if (safe_exists(candidate) &&
            first(mm::mdy::Parser::parse_file(candidate), "kind") == "project") {
            return dir;
        }
        if (!dir.has_relative_path()) break;
    }
    return {};
}

Project load_project(const std::filesystem::path& dir) {
    Project project;

    std::error_code ec;
    WalkState state;
    state.root = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        std::cerr << "build: cannot resolve project root " << dir.string()
                  << ": " << ec.message() << "\n";
        project.ok = false;
        return project;
    }

    walk_project(dir, no_parent, project, state);
    if (!project.ok) return project;

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
            project.ok = false;
            return;
        }
        targets_by_dir[target.dir] = &target;
    };

    for (const auto& target : project.targets) {
        check_dir(target);

        if (target.kind == "module") {
            const auto it = modules_by_name.find(target.module_name);
            if (it != modules_by_name.end()) {
                std::cerr << "build: module: " << target.module_name << " is exported by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                project.ok = false;
            } else {
                modules_by_name[target.module_name] = &target;
            }
        } else if (target.kind == "app") {
            const auto it = apps_by_name.find(target.name);
            if (it != apps_by_name.end()) {
                std::cerr << "build: app name \"" << target.name << "\" is declared by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                project.ok = false;
            } else {
                apps_by_name[target.name] = &target;
            }
        }
    }

    for (const auto& target : project.tests) check_dir(target);
    for (const auto& target : project.docs) check_dir(target);

    return project;
}

// Projections of the single traversal above, kept so callers that want only
// one view need not know about the other.
Tree load_tree(const std::filesystem::path& dir) {
    auto project = load_project(dir);

    Tree tree;
    tree.ok = project.ok;
    tree.targets = std::move(project.targets);
    tree.tests = std::move(project.tests);
    tree.docs = std::move(project.docs);
    return tree;
}

std::vector<Node> load_nodes(const std::filesystem::path& dir, bool& ok) {
    auto project = load_project(dir);
    ok = project.ok;
    return std::move(project.nodes);
}

Target load_test(const std::filesystem::path& manifest_path, bool& ok) {
    ok = false;
    Target target;

    if (!safe_exists(manifest_path)) {
        std::cerr << "build: manifest does not exist: " << manifest_path.string() << "\n";
        return target;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest_path);

    if (!valid_mm_version(doc, manifest_path)) return target;

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

    if (target.name.empty()) {
        std::cerr << "build: manifest has no name\n";
        return target;
    }
    if (!is_safe_name(target.name)) {
        std::cerr << "build: unsafe name \"" << target.name << "\"\n";
        return target;
    }

    // unit: entries are already root relative; see push_source in walk() for
    // why an absolute or ".."-escaping one is rejected rather than joined.
    for (const auto& value : all(doc, "unit")) {
        auto unit = parse_unit(value);
        if (!is_safe_relative_path(unit.path, unit.path)) {
            std::cerr << "build: unsafe source path \"" << unit.path << "\"\n";
            return target;
        }
        target.sources.push_back(std::move(unit));
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
        if (!safe_exists(source.path)) {
            std::cerr << "build: source does not exist: " << source.path << "\n";
            return exit_manifest;
        }

        const auto object = build_dir / (source.path + ".o");
        if (!within_root(object)) {
            std::cerr << "build: refusing to write outside the project: " << object.string() << "\n";
            return exit_manifest;
        }
        std::filesystem::create_directories(object.parent_path(), ec);
        if (ec) {
            std::cerr << "build: cannot create " << object.parent_path().string() << ": "
                      << ec.message() << "\n";
            return exit_compile;
        }

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
    if (!within_root(output)) {
        std::cerr << "build: refusing to link outside the project: " << output.string() << "\n";
        return exit_link;
    }

    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        std::cerr << "build: cannot create " << output.parent_path().string() << ": "
                  << ec.message() << "\n";
        return exit_link;
    }

    auto temp = output;
    temp += ".link-tmp";
    std::filesystem::remove(temp, ec);

    std::string command = toolchain.cxx + " " + toolchain.ldflags;
    for (const auto& object : objects) command += " " + shell_quote(object);
    command += " -o " + shell_quote(temp);

    if (run(toolchain, command) != 0) {
        std::cerr << "build: failed to link " << output.string() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    std::filesystem::rename(temp, output, ec);
    if (ec) {
        std::cerr << "build: failed to install " << output.string() << ": " << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    return exit_ok;
}

int install(const std::filesystem::path& from, const std::filesystem::path& bin_dir,
            const std::string& name) {
    if (!is_safe_name(name)) {
        std::cerr << "build: refusing to install to unsafe name: " << name << "\n";
        return exit_link;
    }

    const auto installed = bin_dir / name;
    if (!within_root(installed)) {
        std::cerr << "build: refusing to install outside the project: " << installed.string() << "\n";
        return exit_link;
    }

    std::error_code ec;
    std::filesystem::create_directories(bin_dir, ec);
    if (ec) {
        std::cerr << "build: cannot create " << bin_dir.string() << ": " << ec.message() << "\n";
        return exit_link;
    }

    // Written to a temporary file and renamed into place, rather than
    // removed and copied: rename() replaces the destination atomically, so
    // there is never a window where installed has just been deleted and not
    // yet replaced, and a failed copy never removes a working binary.
    const auto temp = bin_dir / (name + ".install-tmp");
    std::filesystem::remove(temp, ec);
    std::filesystem::copy_file(from, temp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "build: failed to install " << name << ": " << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    std::filesystem::rename(temp, installed, ec);
    if (ec) {
        std::cerr << "build: failed to install " << name << ": " << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    return exit_ok;
}

bool clear_module_cache() {
    std::error_code ec;
    std::filesystem::remove_all("gcm.cache", ec);
    if (ec) {
        std::cerr << "build: cannot clear gcm.cache: " << ec.message() << "\n";
        return false;
    }
    return true;
}

}
