module;

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

std::string_view option_name(std::string_view declaration) {
    return declaration.substr(0, declaration.find_first_of(" \t"));
}

bool capability_name(std::string_view name) {
    return name == "buildable-host" || name == "buildable-target";
}

std::vector<std::string> capability_declarations(const std::vector<std::string>& declarations) {
    std::vector<std::string> result;
    for (const auto& declaration : declarations)
        if (capability_name(option_name(declaration))) result.push_back(declaration);
    return result;
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
    LoadPolicy policy;
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
        std::cerr << state.policy.tool << ": missing manifest: " << manifest.string() << "\n";
        return Enter::error;
    }

    // Resolves "..", "." and symlinks, so two spellings of one manifest compare
    // equal and a symlink loop cannot masquerade as a new directory.
    std::error_code ec;
    canonical = std::filesystem::weakly_canonical(manifest, ec);
    if (ec) {
        std::cerr << state.policy.tool << ": cannot resolve " << manifest.string() << ": " << ec.message() << "\n";
        return Enter::error;
    }

    const auto relative = canonical.lexically_relative(state.root);
    if (relative.empty() || *relative.begin() == "..") {
        std::cerr << state.policy.tool << ": manifest outside the project root: " << canonical.string() << "\n";
        return Enter::error;
    }

    if (state.contains(state.visiting, canonical)) {
        std::cerr << state.policy.tool << ": folder: cycle in the manifest tree:\n";
        for (const auto& entry : state.visiting)
            std::cerr << "    " << entry.lexically_relative(state.root).string() << "\n";
        std::cerr << "    " << relative.string() << "  <- repeats\n";
        return Enter::error;
    }

    if (state.policy.strict_tree) {
        // Generated lanes are root siblings: out is the bootstrap/configuration
        // tree, while configured host and target lanes use out-* names. Match
        // the complete first component so a source directory such as "outside"
        // is not rejected merely because its name begins with those letters.
        const auto first = relative.begin()->generic_string();
        if (first == "out" || first.starts_with("out-")) {
            std::cerr << state.policy.tool << ": generated output directory in manifest tree: "
                      << manifest.string() << "\n";
            return Enter::error;
        }
    }
    if (state.contains(state.visited, canonical)) {
        if (!state.policy.strict_tree) return Enter::skip;
        std::cerr << state.policy.tool << ": repeated canonical manifest directory: "
                  << manifest.string() << "\n";
        return Enter::error;
    }

    return Enter::ok;
}

// The kind and name every manifest must declare to be usable further,
// independent of whether the walk that reached it builds a BuildableNode (walk) or
// only records a ManifestNode (load_nodes' walk_nodes). Both walks share it: without
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

// All source-manifest consumers share this gate. Configuration records have
// their own schema and do not pass through it.
bool valid_mm_version(const mm::mdy::MDYDocument& doc, const std::filesystem::path& manifest,
                      const LoadPolicy& policy) {
    const auto* versions = lookup(doc, "mm");
    if (doc.status != mm::mdy::ParseStatus::Ok || versions == nullptr ||
        versions->size() != 1 || (versions->front() != "1.0" && versions->front() != "1.1")) {
        std::cerr << policy.tool << ": invalid or unsupported mm: version in "
                  << manifest.string() << " (supported: 1.0, 1.1)\n";
        return false;
    }
    for (const auto& [key, values] : doc.metadata) {
        const bool option = key == "option" || key == "reset" || key == "read-only";
        if (option && versions->front() == "1.0") {
            std::cerr << policy.tool << ": " << manifest.string() << ": " << key
                      << " requires mm: 1.1\n";
            return false;
        }
        const bool known = option || key == "mm" || key == "kind" || key == "name" ||
                           key == "module" || key == "folder" || key == "file" ||
                           key == "unit" || key == "use";
        if (!known && versions->front() == "1.1") {
            std::cerr << policy.tool << ": " << manifest.string()
                      << ": unknown manifest key: " << key
                      << (policy.strict_tree ? "\n" : " (ignored)\n");
            if (policy.strict_tree) return false;
        }
        if (option && policy.warn_options) {
            for (const auto& value : values) {
                const auto name = value.substr(0, value.find_first_of(" \t"));
                if (capability_name(name)) continue;
                std::cerr << policy.tool << ": " << manifest.string() << ": " << key
                          << " " << name << " is ignored; continuing with existing build configuration\n";
            }
        }
    }
    return true;
}

bool configuration_scalar(const mm::mdy::MDYDocument& doc, std::string_view key,
                          const std::filesystem::path& path, std::string& value) {
    const auto* values = lookup(doc, key);
    if (values == nullptr || values->size() != 1 || values->front().empty()) {
        std::cerr << "build: configuration requires one non-empty " << key << ": "
                  << path.string() << "\n";
        return false;
    }
    value = values->front();
    return true;
}

bool configuration_build(const mm::mdy::MDYDocument& doc,
                         const std::filesystem::path& path, Build& build) {
    const auto* values = lookup(doc, "build");
    if (values == nullptr) {
        build = Build::Debug;
        return true;
    }
    if (values->size() != 1 || values->front().empty()) {
        std::cerr << "build: configuration requires one non-empty build: " << path.string()
                  << "\n";
        return false;
    }
    if (values->front() == "debug") {
        build = Build::Debug;
        return true;
    }
    if (values->front() == "release") {
        build = Build::Release;
        return true;
    }
    std::cerr << "build: configuration build must be debug or release: " << path.string()
              << "\n";
    return false;
}

bool configuration_boolean(const mm::mdy::MDYDocument& document, std::string_view key,
                           const std::filesystem::path& path, bool default_value,
                           bool& value) {
    const auto* values = lookup(document, key);
    if (values == nullptr) {
        value = default_value;
        return true;
    }
    if (values->size() != 1 || (values->front() != "yes" && values->front() != "no")) {
        std::cerr << "build: configuration " << key << " must be yes or no: "
                  << path.string() << "\n";
        return false;
    }
    value = values->front() == "yes";
    return true;
}

bool configuration_directory(const mm::mdy::MDYDocument& doc, std::string_view key,
                             const std::filesystem::path& path,
                             std::filesystem::path& directory) {
    std::string value;
    if (!configuration_scalar(doc, key, path, value)) return false;

    directory = value;
    const auto normalized = directory.lexically_normal();
    if (directory.is_absolute() || normalized.empty() || normalized == ".") {
        std::cerr << "build: configuration has unsafe " << key << ": " << value << "\n";
        return false;
    }
    for (const auto& component : normalized) {
        if (component != "..") continue;
        std::cerr << "build: configuration has unsafe " << key << ": " << value << "\n";
        return false;
    }

    directory = normalized;
    return true;
}

bool configuration_compiler(const mm::mdy::MDYDocument& document, std::string_view prefix,
                            const std::filesystem::path& path, Toolchain& toolchain) {
    std::string platform;
    const auto family_key = std::string(prefix) + "-compiler-family";
    const auto* family_values = lookup(document, family_key);
    if (family_values != nullptr) {
        if (family_values->size() != 1 || family_values->front().empty()) {
            std::cerr << "build: configuration requires one non-empty " << family_key << ": "
                      << path.string() << "\n";
            return false;
        }
        if (family_values->front() == "gcc")
            toolchain.family = CompilerFamily::Gcc;
        else if (family_values->front() == "clang")
            toolchain.family = CompilerFamily::Clang;
        else {
            std::cerr << "build: configuration " << family_key << " must be gcc or clang: "
                      << path.string() << "\n";
            return false;
        }
    }

    if (!configuration_scalar(document, std::string(prefix) + "-compiler", path,
                              toolchain.compiler.invocation) ||
        !configuration_scalar(document, std::string(prefix) + "-target", path,
                              toolchain.target) ||
        !configuration_scalar(document, std::string(prefix) + "-platform", path, platform) ||
        !configuration_scalar(document, std::string(prefix) + "-compile-flags", path,
                              toolchain.compiler.arguments) ||
        !configuration_scalar(document, std::string(prefix) + "-link-flags", path,
                              toolchain.linker.arguments))
        return false;

    if (platform != "POSIX") {
        std::cerr << "build: configuration names unsupported " << prefix
                  << " platform: " << platform << "\n";
        return false;
    }
    toolchain.assembler.invocation = toolchain.compiler.invocation;
    toolchain.linker.invocation = toolchain.compiler.invocation;
    toolchain.librarian = {};
    toolchain.debugger = {};
    return true;
}

bool has_configuration_compiler(const mm::mdy::MDYDocument& document,
                                std::string_view prefix) {
    return lookup(document, std::string(prefix) + "-compiler-family") != nullptr ||
           lookup(document, std::string(prefix) + "-compiler") != nullptr ||
           lookup(document, std::string(prefix) + "-target") != nullptr ||
           lookup(document, std::string(prefix) + "-platform") != nullptr ||
           lookup(document, std::string(prefix) + "-compile-flags") != nullptr ||
           lookup(document, std::string(prefix) + "-link-flags") != nullptr;
}

bool configuration_runner(const mm::mdy::MDYDocument& document,
                          const std::filesystem::path& path,
                          std::optional<ToolchainRunner>& result) {
    const auto* invocation = lookup(document, "cross-runner");
    const bool has_any = invocation != nullptr ||
        lookup(document, "cross-runner-prefix-argument") != nullptr ||
        lookup(document, "cross-runner-image") != nullptr ||
        lookup(document, "cross-runner-image-option") != nullptr ||
        lookup(document, "cross-runner-suffix-argument") != nullptr ||
        lookup(document, "cross-runner-forwards-arguments") != nullptr;
    if (!has_any) {
        result.reset();
        return true;
    }
    if (invocation == nullptr || invocation->size() != 1 || invocation->front().empty()) {
        std::cerr << "build: configuration requires one non-empty cross-runner: "
                  << path.string() << "\n";
        return false;
    }

    ToolchainRunner runner;
    runner.invocation = invocation->front();
    if (const auto* values = lookup(document, "cross-runner-prefix-argument"))
        runner.prefix_arguments = *values;
    if (const auto* values = lookup(document, "cross-runner-suffix-argument"))
        runner.suffix_arguments = *values;

    std::string image;
    if (!configuration_scalar(document, "cross-runner-image", path, image)) return false;
    if (image == "positional") {
        runner.image = RunnerImage::Positional;
        if (lookup(document, "cross-runner-image-option") != nullptr) {
            std::cerr << "build: positional runner cannot have cross-runner-image-option: "
                      << path.string() << "\n";
            return false;
        }
    } else if (image == "option") {
        runner.image = RunnerImage::Option;
        if (!configuration_scalar(document, "cross-runner-image-option", path,
                                  runner.image_option))
            return false;
    } else {
        std::cerr << "build: configuration cross-runner-image must be positional or option: "
                  << path.string() << "\n";
        return false;
    }
    if (!configuration_boolean(document, "cross-runner-forwards-arguments", path, true,
                               runner.forwards_arguments))
        return false;
    result = std::move(runner);
    return true;
}

bool valid_manifest(const mm::mdy::MDYDocument& doc, std::string_view kind, std::string_view name,
                    const std::filesystem::path& manifest, const LoadPolicy& policy) {
    if (!valid_mm_version(doc, manifest, policy)) return false;
    if (kind != "project" && kind != "dir" && kind != "module" &&
        kind != "app" && kind != "test" && kind != "doc") {
        std::cerr << policy.tool << ": unknown kind \"" << kind << "\" in " << manifest.string() << "\n";
        return false;
    }
    if (name.empty()) {
        std::cerr << policy.tool << ": manifest has no name: " << manifest.string() << "\n";
        return false;
    }
    if (!is_safe_name(name)) {
        std::cerr << policy.tool << ": unsafe name \"" << name << "\" in " << manifest.string() << "\n";
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

    if (!valid_manifest(doc, kind, name, manifest, state.policy)) {
        project.ok = false;
        state.visited.push_back(canonical);
        return;
    }

    ManifestNode node;
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

    BuildableNode target;
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
            std::cerr << state.policy.tool << ": unsafe source path \"" << unit.path << "\" in "
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
            std::cerr << state.policy.tool << ": manifest declares no unit: entries: " << manifest.string() << "\n";
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
        std::cerr << state.policy.tool << ": manifest declares no file: entries: " << manifest.string() << "\n";
        project.ok = false;
        return;
    }
    if (kind == "module" && target.module_name.empty()) {
        std::cerr << state.policy.tool << ": module manifest has no module: name: " << manifest.string() << "\n";
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
    toolchain.verbose = verbose;
    return toolchain;
}

bool validate_manifest_schema(const mm::mdy::MDYDocument& document,
                              const std::filesystem::path& manifest, const LoadPolicy& policy) {
    return valid_manifest(document, first(document, "kind"), first(document, "name"), manifest, policy);
}

bool load_configuration(const std::filesystem::path& path, bool verbose,
                        BuildConfiguration& configuration) {
    const auto document = mm::mdy::Parser::parse_file(path);
    if (document.status != mm::mdy::ParseStatus::Ok) {
        std::cerr << "build: cannot read configuration: " << path.string() << "\n";
        return false;
    }

    std::string version;
    std::string kind;
    std::string name;
    std::string selection;
    if (!configuration_scalar(document, "mm", path, version) || version != "1.0" ||
        !configuration_scalar(document, "kind", path, kind) || kind != "configuration" ||
        !configuration_scalar(document, "name", path, name) ||
        !configuration_scalar(document, "target-compiler", path, selection)) {
        std::cerr << "build: invalid configuration: " << path.string() << "\n";
        return false;
    }

    if (selection != "host" && selection != "cross") {
        std::cerr << "build: configuration target-compiler must be host or cross: "
                  << path.string() << "\n";
        return false;
    }

    Build build;
    if (!configuration_build(document, path, build)) return false;

    Toolchain host;
    if (!configuration_compiler(document, "host", path, host)) return false;

    Toolchain cross;
    const bool has_cross = has_configuration_compiler(document, "cross");
    if (has_cross && !configuration_compiler(document, "cross", path, cross)) return false;

    std::optional<ToolchainRunner> cross_runner;
    if (!configuration_runner(document, path, cross_runner)) return false;
    if (cross_runner && !has_cross) {
        std::cerr << "build: configuration gives a runner to a missing target: "
                  << path.string() << "\n";
        return false;
    }
    if (cross_runner) cross.runner = std::move(cross_runner);

    if (selection == "cross") {
        if (!has_cross) {
            std::cerr << "build: configuration selects cross without a cross compiler: "
                      << path.string() << "\n";
            return false;
        }
    }
    bool target_has_host_capability = false;
    if (!configuration_boolean(document, "target-host-capability", path, false,
                               target_has_host_capability))
        return false;
    if (target_has_host_capability && !has_cross) {
        std::cerr << "build: configuration gives host capability to a missing target: "
                  << path.string() << "\n";
        return false;
    }

    std::filesystem::path host_directory;
    std::filesystem::path target_directory;
    if (!configuration_directory(document, "host-build-directory", path, host_directory) ||
        !configuration_directory(document, "target-build-directory", path, target_directory))
        return false;

    host.verbose = verbose;
    if (has_cross) cross.verbose = verbose;
    configuration.host_ = std::move(host);
    configuration.cross_ = has_cross ? std::optional<Toolchain>(std::move(cross)) : std::nullopt;
    configuration.cross_build_directory_ =
        has_cross ? std::optional<std::filesystem::path>(target_directory) : std::nullopt;
    configuration.selects_cross_ = selection == "cross";
    configuration.target_has_host_capability_ = target_has_host_capability;
    configuration.host_build_directory = host_directory;
    configuration.build = build;
    configuration.build_directory = selection == "cross" ? std::move(target_directory)
                                                           : std::move(host_directory);
    return true;
}

bool resolve_configuration(const std::filesystem::path& project_root, bool verbose,
                           BuildConfiguration& configuration) {
    const auto path = project_root / "out" / "config.mdy";
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        std::cerr << "build: cannot check " << path.string() << ": " << ec.message() << "\n";
        return false;
    }

    if (exists) return load_configuration(path, verbose, configuration);

    configuration.host_ = default_toolchain(verbose);
    configuration.cross_.reset();
    configuration.cross_build_directory_.reset();
    configuration.selects_cross_ = false;
    configuration.target_has_host_capability_ = false;
    configuration.build = Build::Debug;
    configuration.build_directory = "out";
    configuration.host_build_directory = "out";
    return true;
}

TranslationUnit parse_unit(std::string_view value) {
    TranslationUnit unit;

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

bool validate_capabilities(const std::vector<mm::configure::OptionNode>& nodes,
                           const std::vector<mm::configure::OptionValues>& resolved,
                           std::string_view tool) {
    if (nodes.size() != resolved.size()) return false;

    // use: names a module, and the resolver is indexed by node, so the edge
    // needs a module name to node index map. index_of_module answers with a
    // targets index instead, which is the wrong side of the join here.
    std::map<std::string, std::size_t, std::less<>> modules;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == "module" && !nodes[i].module_name.empty())
            modules.emplace(nodes[i].module_name, i);

    const auto capability = [&](std::size_t node, std::string_view lane) {
        return resolved[node].find(lane)->second;
    };

    bool ok = true;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (const auto& used : nodes[i].uses) {
            const auto found = modules.find(used);
            if (found == modules.end()) continue;  // order() reports unknown modules
            for (const auto& lane : {std::string_view("buildable-host"),
                                     std::string_view("buildable-target")}) {
                const auto consumer = capability(i, lane);
                const auto dependency = capability(found->second, lane);
                if (!consumer.boolean || dependency.boolean) continue;
                std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                          << nodes[i].name << " is " << lane << ", but " << used
                          << " is not (" << lane << " no, "
                          << (dependency.origin == mm::configure::OptionOrigin::Assignment
                                  ? "assigned by " : "reset by ")
                          << dependency.value_source.generic_string() << ")\n";
                ok = false;
            }
        }
    }
    return ok;
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

Project load_project(const std::filesystem::path& dir, const LoadPolicy& policy) {
    Project project;

    std::error_code ec;
    WalkState state;
    state.policy = policy;
    state.root = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        std::cerr << policy.tool << ": cannot resolve project root " << dir.string()
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
    std::map<std::string, const BuildableNode*, std::less<>> modules_by_name;
    std::map<std::string, const BuildableNode*, std::less<>> apps_by_name;
    std::map<std::filesystem::path, const BuildableNode*> targets_by_dir;

    auto check_dir = [&](const BuildableNode& target) {
        const auto it = targets_by_dir.find(target.dir);
        if (it != targets_by_dir.end()) {
            std::cerr << policy.tool << ": " << target.dir.string() << " is declared by more than one manifest: "
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
                std::cerr << policy.tool << ": module: " << target.module_name << " is exported by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                project.ok = false;
            } else {
                modules_by_name[target.module_name] = &target;
            }
        } else if (target.kind == "app") {
            const auto it = apps_by_name.find(target.name);
            if (it != apps_by_name.end()) {
                std::cerr << policy.tool << ": app name \"" << target.name << "\" is declared by both "
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


std::vector<mm::configure::OptionNode> configuration_nodes(const Project& project) {
    std::vector<mm::configure::OptionNode> nodes;
    if (!project.ok || project.nodes.size() != project.documents.size()) return nodes;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        const auto& doc = project.documents[i];
        nodes.push_back({node.manifest, node.dir, node.name, node.kind,
                         first(doc, "module"), all(doc, "use"), node.parent,
                         all(doc, "option"), all(doc, "reset"), all(doc, "read-only")});
    }
    return nodes;
}

bool resolve_capabilities(const std::filesystem::path& project_root, Build build,
                          const Project& project, BuildCapabilities& capabilities,
                          std::string_view tool) {
    auto nodes = configuration_nodes(project);
    for (auto& node : nodes) {
        node.options = capability_declarations(node.options);
        node.resets = capability_declarations(node.resets);
        node.read_only = capability_declarations(node.read_only);
    }

    std::vector<mm::configure::OptionValues> resolved;
    if (!mm::configure::resolve_options(project_root, build, nodes, resolved, tool) ||
        !validate_capabilities(nodes, resolved, tool))
        return false;

    capabilities.host.clear();
    capabilities.target.clear();
    capabilities.host.reserve(resolved.size());
    capabilities.target.reserve(resolved.size());
    for (const auto& values : resolved) {
        capabilities.host.push_back(values.find("buildable-host")->second.boolean);
        capabilities.target.push_back(values.find("buildable-target")->second.boolean);
    }
    return true;
}

std::vector<bool> BuildCapabilities::lane(bool target_lane,
                                          bool target_has_host_capability) const {
    if (!target_lane) return host;
    if (!target_has_host_capability) return target;

    std::vector<bool> result;
    result.reserve(target.size());
    for (std::size_t i = 0; i < target.size(); ++i)
        result.push_back(target[i] || host[i]);
    return result;
}

// Projections of the single traversal above, kept so callers that want only
// one view need not know about the other.
Tree load_tree(const std::filesystem::path& dir, const LoadPolicy& policy) {
    auto project = load_project(dir, policy);

    Tree tree;
    tree.ok = project.ok;
    tree.targets = std::move(project.targets);
    tree.tests = std::move(project.tests);
    tree.docs = std::move(project.docs);
    return tree;
}

std::vector<ManifestNode> load_nodes(const std::filesystem::path& dir, bool& ok,
                                     const LoadPolicy& policy) {
    auto project = load_project(dir, policy);
    ok = project.ok;
    return std::move(project.nodes);
}

BuildableNode load_test(const std::filesystem::path& manifest_path, bool& ok,
                        const LoadPolicy& policy) {
    ok = false;
    BuildableNode target;

    if (!safe_exists(manifest_path)) {
        std::cerr << policy.tool << ": manifest does not exist: " << manifest_path.string() << "\n";
        return target;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest_path);

    if (!valid_mm_version(doc, manifest_path, policy)) return target;

    const auto kind = first(doc, "kind");
    if (kind != "test") {
        std::cerr << policy.tool << ": manifest kind is \"" << kind << "\", expected \"test\"\n";
        return target;
    }

    target.kind = kind;
    target.name = first(doc, "name");
    target.module_name = first(doc, "module");
    target.dir = manifest_path.parent_path();
    target.uses = all(doc, "use");

    if (target.name.empty()) {
        std::cerr << policy.tool << ": manifest has no name\n";
        return target;
    }
    if (!is_safe_name(target.name)) {
        std::cerr << policy.tool << ": unsafe name \"" << target.name << "\"\n";
        return target;
    }

    // unit: entries are already root relative; see push_source in walk() for
    // why an absolute or ".."-escaping one is rejected rather than joined.
    for (const auto& value : all(doc, "unit")) {
        auto unit = parse_unit(value);
        if (!is_safe_relative_path(unit.path, unit.path)) {
            std::cerr << policy.tool << ": unsafe source path \"" << unit.path << "\"\n";
            return target;
        }
        target.sources.push_back(std::move(unit));
    }

    if (target.sources.empty()) {
        std::cerr << policy.tool << ": manifest declares no unit: entries\n";
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

int execute(const Toolchain& toolchain, bool target_lane,
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments) {
    std::string command;
    if (!target_lane) {
        command = shell_quote(executable);
        for (const auto& argument : arguments)
            command += " " + shell_quote(std::filesystem::path(argument));
        return run(toolchain, command);
    }
    if (!toolchain.runner) return -1;

    const auto& runner = *toolchain.runner;
    command = shell_quote(std::filesystem::path(runner.invocation));
    for (const auto& argument : runner.prefix_arguments)
        command += " " + shell_quote(std::filesystem::path(argument));
    if (runner.image == RunnerImage::Option)
        command += " " + shell_quote(std::filesystem::path(runner.image_option));
    command += " " + shell_quote(executable);
    for (const auto& argument : runner.suffix_arguments)
        command += " " + shell_quote(std::filesystem::path(argument));
    if (runner.forwards_arguments)
        for (const auto& argument : arguments)
            command += " " + shell_quote(std::filesystem::path(argument));
    return run(toolchain, command);
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

int compile(const Toolchain& toolchain, BuildableNode& target, const std::filesystem::path& build_dir) {
    std::error_code ec;

    const auto bmi_dir = build_dir / "bmi";
    if (toolchain.family == CompilerFamily::Clang) {
        if (!within_root(bmi_dir)) {
            std::cerr << "build: refusing to write outside the project: " << bmi_dir.string()
                      << "\n";
            return exit_manifest;
        }
        std::filesystem::create_directories(bmi_dir, ec);
        if (ec) {
            std::cerr << "build: cannot create " << bmi_dir.string() << ": " << ec.message()
                      << "\n";
            return exit_compile;
        }
    }

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

        std::string command = toolchain.compiler.invocation + " " + toolchain.compiler.arguments;
        if (toolchain.family == CompilerFamily::Gcc) {
            command += " -fmodules-ts -x c++";
        } else {
            command += " -fprebuilt-module-path=" + shell_quote(bmi_dir);

            std::string module_name = source.module_name;
            if (module_name.empty() && target.kind == "module" &&
                std::filesystem::path(source.path).extension() == ".cppm")
                module_name = target.module_name;

            if (!module_name.empty()) {
                for (char& c : module_name)
                    if (c == ':') c = '-';
                const auto bmi = bmi_dir / (module_name + ".pcm");
                if (!within_root(bmi)) {
                    std::cerr << "build: refusing to write outside the project: " << bmi.string()
                              << "\n";
                    return exit_manifest;
                }
                command += " -fmodule-output=" + shell_quote(bmi);
            }
        }
        command += " -c " + shell_quote(source.path) + " -o " + shell_quote(object);
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

    std::string command = toolchain.linker.invocation + " " + toolchain.linker.arguments;
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

bool clear_module_cache(const std::filesystem::path& build_dir) {
    std::error_code ec;
    std::filesystem::remove_all("gcm.cache", ec);
    if (ec) {
        std::cerr << "build: cannot clear gcm.cache: " << ec.message() << "\n";
        return false;
    }

    const auto bmi_dir = build_dir / "bmi";
    if (!within_root(bmi_dir)) {
        std::cerr << "build: refusing to clear outside the project: " << bmi_dir.string() << "\n";
        return false;
    }
    std::filesystem::remove_all(bmi_dir, ec);
    if (ec) {
        std::cerr << "build: cannot clear " << bmi_dir.string() << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}

}
