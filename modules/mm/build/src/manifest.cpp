// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module mm.build;

import mm.configure;
import mm.json;
import mm.mdy;
import :detail;
import :config;
import :manifest;
import :compile;
import :graph;

namespace mm::build {
// This unit implements the mm.build:manifest partition: manifest validation, the project walk, definitions, and project loading.
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

bool structural_property_name(std::string_view name) {
    return name == "buildable-host" || name == "buildable-target" || name == "core";
}
std::vector<std::string> structural_property_declarations(
    const std::vector<std::string>& declarations) {
    std::vector<std::string> result;
    for (const auto& declaration : declarations)
        if (structural_property_name(option_name(declaration))) result.push_back(declaration);
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
    std::filesystem::path external_root;          // canonical external root
    std::filesystem::path external_root_display;  // caller spelling for public paths
    bool is_external = false;
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

    const auto owning_root =
        state.is_external ? state.external_root : state.root;
    const auto relative = canonical.lexically_relative(owning_root);
    if (relative.empty() || *relative.begin() == "..") {
        std::cerr << state.policy.tool << ": manifest outside the "
                  << (state.is_external ? "external" : "project")
                  << " root: " << canonical.string() << "\n";
        return Enter::error;
    }

    if (state.contains(state.visiting, canonical)) {
        std::cerr << state.policy.tool << ": folder: cycle in the manifest tree:\n";
        for (const auto& entry : state.visiting)
            std::cerr << "    "
                      << entry.lexically_relative(owning_root).string() << "\n";
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
// included, with no check that it named one of the nine kinds this project
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

bool path_within(const std::filesystem::path& base, const std::filesystem::path& path) {
    const auto relative = path.lexically_normal().lexically_relative(base.lexically_normal());
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

bool path_contained_in(const std::filesystem::path& container,
                       const std::filesystem::path& path) {
    if (container.empty() || path.empty()) return false;
    std::error_code ec;
    auto resolved_container = std::filesystem::weakly_canonical(container, ec);
    if (ec) return false;
    if (!resolved_container.is_absolute())
        resolved_container =
            std::filesystem::absolute(container, ec).lexically_normal();
    if (ec) return false;

    auto resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec) return false;
    if (!resolved.is_absolute())
        resolved = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return false;

    const auto relative = resolved.lexically_relative(resolved_container);
    return !relative.empty() && *relative.begin() != "..";
}
bool within_root(const std::filesystem::path& path) {
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec);
    if (ec) return false;
    return path_contained_in(root, path);
}
bool valid_manifest(const mm::mdy::MDYDocument& doc, std::string_view kind, std::string_view name,
                    const std::filesystem::path& manifest, const LoadPolicy& policy) {
    if (!valid_mm_version(doc, manifest, policy)) return false;
    if (kind == "library" && manifest_version(first(doc, "mm"))->number < 12) {
        std::cerr << policy.tool << ": " << manifest.string()
                  << ": kind library requires mm: 1.2\n";
        return false;
    }
    if (kind != "project" && kind != "dir" && kind != "module" &&
        kind != "app" && kind != "test" && kind != "doc" &&
        kind != "sdk" && kind != "board" && kind != "library") {
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
    if (kind == "board" && !is_safe_board_name(name)) {
        std::cerr << policy.tool << ": unsafe board name \"" << name << "\" in " << manifest.string() << "\n";
        return false;
    }
    return true;
}
// A library's folder: entries reach the project-authored wrappers beside its
// vendored tree, never into it. file: being invalid on a library manifest keeps
// the manifest itself from naming foreign source, but it does not stop a folder:
// entry pointing at the checkout, and a manifest found there would produce
// ordinary targets that compile and are handed to check. The canonical
// comparison is what closes the door: a lexically unrelated folder: can still be
// a symlink into the tree.
bool folder_within_library_source(const std::filesystem::path& source,
                                  const std::filesystem::path& folder) {
    if (path_within(source, folder)) return true;

    // weakly_canonical leaves a path that does not exist yet relative, the same
    // case within_root documents; fall back to lexical resolution so the two
    // sides are always compared in the same form.
    const auto resolve = [](const std::filesystem::path& path) {
        std::error_code ec;
        auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (ec) return std::filesystem::path{};
        if (!resolved.is_absolute())
            resolved = std::filesystem::absolute(path, ec).lexically_normal();
        return ec ? std::filesystem::path{} : resolved;
    };

    const auto canonical_source = resolve(source);
    const auto canonical_folder = resolve(folder);
    if (canonical_source.empty() || canonical_folder.empty()) return false;
    return path_within(canonical_source, canonical_folder);
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

    const auto* proj_lookup = lookup(doc, "project");
    if (proj_lookup != nullptr && !proj_lookup->empty()) {
        if (proj_lookup->size() > 1) {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": duplicate project: declaration\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        if (kind != "app" && kind != "dir") {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": project: is only allowed on app and dir"
                      << " manifests\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        if (!state.is_external) {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": project: is not allowed inside a project tree\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        if (parent != 0) {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": project: is only allowed on the root manifest"
                      << " of an external tree\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        const auto& proj_val = proj_lookup->front();
        const std::filesystem::path raw_proj(proj_val);
        if (raw_proj.is_absolute()) {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": project: value must be a relative path\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        const auto resolved_proj =
            (canonical.parent_path() / raw_proj).lexically_normal();
        std::error_code ec_proj;
        const auto canonical_proj =
            std::filesystem::weakly_canonical(resolved_proj, ec_proj);
        if (ec_proj || !safe_exists(canonical_proj / "mm.mdy") ||
            first(mm::mdy::Parser::parse_file(canonical_proj / "mm.mdy"),
                  "kind") != "project") {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": project: does not resolve to a project directory\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        if (path_contained_in(state.external_root, canonical_proj)) {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": project directory is inside the external tree\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
    } else if (state.is_external && parent == 0) {
        std::cerr << state.policy.tool << ": " << manifest.string()
                  << ": external root manifest must declare project:\n";
        project.ok = false;
        state.visited.push_back(canonical);
        return;
    }

    if (state.is_external) {
        if (kind != "dir" && kind != "app") {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": grafted node must be dir or app (found " << kind
                      << ")\n";
            project.ok = false;
            state.visited.push_back(canonical);
            return;
        }
        if (kind == "app") {
            if (lookup(doc, "folder") != nullptr) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": folder: is not allowed on external app"
                          << " manifests\n";
                project.ok = false;
                state.visited.push_back(canonical);
                return;
            }
            if (all(doc, "sketch").empty()) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": external app manifest must declare sketch:\n";
                project.ok = false;
                state.visited.push_back(canonical);
                return;
            }
        }
    }

    ManifestNode node;
    node.manifest = manifest;
    node.dir = state.policy.tool == "configure" ? canonical.parent_path()
                                                  : dir.lexically_normal();
    node.source_dir = state.is_external && state.policy.tool == "build"
        ? dir.lexically_normal() : canonical.parent_path();
    const auto owning_root =
        state.is_external ? state.external_root : state.root;
    const auto rel_logical =
        canonical.parent_path().lexically_relative(owning_root);
    node.logical_dir =
        rel_logical.empty() ? std::filesystem::path(".") : rel_logical;
    node.kind = kind;
    node.name = name;
    node.parent = parent;
    node.external = state.is_external;
    node.non_core = state.is_external;

    project.nodes.push_back(std::move(node));
    const auto index = project.nodes.size() - 1;

    // documents and target stay parallel to nodes, so every push here is
    // matched by one in each.
    project.documents.push_back(doc);
    project.target.push_back(no_target);

    if (parent != no_parent) project.nodes[parent].children.push_back(index);

    if (kind == "project" || kind == "dir" || kind == "library") {
        std::filesystem::path library_source;
        if (kind == "library") {
            const auto declared = first(doc, "source");
            if (!declared.empty()) library_source = (dir / declared).lexically_normal();
        }

        state.visiting.push_back(canonical);
        for (const auto& folder : all(doc, "folder")) {
            if (!library_source.empty() &&
                folder_within_library_source(library_source,
                                             (dir / folder).lexically_normal())) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": folder is inside library source: " << folder << "\n";
                project.ok = false;
                continue;
            }
            walk_project(dir / folder, index, project, state);
        }
        state.visiting.pop_back();
        state.visited.push_back(canonical);
        return;
    }

    // A leaf manifest is finished the moment it is read, and must be marked
    // so before its target is built: a diamond reaches the same folder from
    // two parents, and only this stops the second visit building a second,
    // duplicate target for it.
    state.visited.push_back(canonical);

    if (kind == "sdk" || kind == "board") return;

    BuildableNode target;
    target.kind = kind;
    target.name = name;
    target.module_name = first(doc, "module");
    target.dir = state.policy.tool == "configure" ? canonical.parent_path()
                                                    : dir.lexically_normal();
    target.source_dir = state.is_external && state.policy.tool == "build"
        ? dir.lexically_normal() : canonical.parent_path();
    target.logical_dir = project.nodes[index].logical_dir;
    target.uses = all(doc, "use");
    target.requires_board = first(doc, "requires-board");
    target.external = project.nodes[index].external;
    target.non_core = project.nodes[index].non_core;

    // A marker, not a value: the module: declaration already names the
    // interface, so repeating it here would only create a mismatch to
    // diagnose. kind is already restricted to module by the key table.
    if (const auto* marker = lookup(doc, "platform-interface"); marker != nullptr) {
        if (marker->size() != 1 || !marker->front().empty()) {
            std::cerr << state.policy.tool << ": " << manifest.string()
                      << ": platform-interface is a marker and takes no value\n";
            project.ok = false;
            return;
        }
        target.platform_interface = true;
    }

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
        const auto abs_source = (target.source_dir / raw).lexically_normal();
        if (state.is_external) {
            if (!path_contained_in(state.external_root, abs_source)) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": source outside tree: " << raw.string() << "\n";
                project.ok = false;
                return false;
            }
        }
        unit.path = (state.is_external && join_with_dir)
            ? (target.logical_dir / raw).lexically_normal().string()
            : joined.lexically_normal().string();
        unit.source = abs_source;
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

    if (kind == "app") {
        for (const auto& sketch : all(doc, "sketch")) {
            const std::filesystem::path raw_sketch(sketch);
            const std::filesystem::path joined_sketch = dir / raw_sketch;
            if (!is_safe_relative_path(raw_sketch, joined_sketch)) {
                std::cerr << state.policy.tool << ": unsafe sketch path \""
                          << sketch << "\" in "
                          << manifest.string() << "\n";
                project.ok = false;
                return;
            }
            const auto abs_sketch =
                (target.source_dir / raw_sketch).lexically_normal();
            if (!path_contained_in(owning_root, abs_sketch)) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": sketch source outside tree: " << sketch << "\n";
                project.ok = false;
                return;
            }
            target.sketches.push_back(sketch);
        }
        for (const auto& entry : all(doc, "sketch-library")) {
            if (target.sketches.empty()) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": sketch-library requires sketch:\n";
                project.ok = false;
                return;
            }
            const std::filesystem::path raw_lib(entry);
            if (raw_lib.is_absolute()) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": sketch-library must be relative: " << entry
                          << "\n";
                project.ok = false;
                return;
            }
            // Canonical, not merely normalised: a value that climbs leaves a
            // trailing separator behind, and this path is compared, stored,
            // and handed to the compiler as one -I argument.
            std::error_code root_ec;
            const auto lib_root = std::filesystem::weakly_canonical(
                target.source_dir / raw_lib, root_ec);
            if (root_ec || !path_contained_in(owning_root, lib_root)) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": sketch-library outside tree: " << entry << "\n";
                project.ok = false;
                return;
            }
            std::error_code lib_ec;
            if (!std::filesystem::is_directory(lib_root, lib_ec) || lib_ec) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": sketch-library is not a directory: " << entry
                          << "\n";
                project.ok = false;
                return;
            }

            // The two sketch library layouts: a src/ directory owns the
            // sources and the include path when it exists, otherwise the root
            // does, and neither descends into examples/ or test/.
            const auto src_dir = lib_root / "src";
            const bool layered =
                std::filesystem::is_directory(src_dir, lib_ec) && !lib_ec;
            const auto compiled_root = layered ? src_dir : lib_root;

            std::vector<std::filesystem::path> library_sources;
            const auto collect = [&](const std::filesystem::path& file) {
                const auto ext = file.extension().string();
                if (ext == ".cpp" || ext == ".cc" || ext == ".c")
                    library_sources.push_back(file);
            };
            if (layered) {
                using Walk = std::filesystem::recursive_directory_iterator;
                for (const auto& item : Walk(compiled_root, lib_ec)) {
                    if (item.is_regular_file()) collect(item.path());
                }
            } else {
                using Walk = std::filesystem::directory_iterator;
                for (const auto& item : Walk(compiled_root, lib_ec)) {
                    if (item.is_regular_file()) collect(item.path());
                }
            }
            if (lib_ec) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": cannot read sketch-library " << entry << ": "
                          << lib_ec.message() << "\n";
                project.ok = false;
                return;
            }
            // Directory order is not defined; the link command is.
            std::sort(library_sources.begin(), library_sources.end());

            auto displayed_library = (target.source_dir / raw_lib).lexically_normal();
            if (state.is_external && lib_root == state.external_root &&
                !state.external_root_display.empty())
                displayed_library = state.external_root_display;
            target.sketch_libraries.push_back(std::move(displayed_library));
            const auto displayed_compiled_root =
                (layered ? target.sketch_libraries.back() / "src"
                          : target.sketch_libraries.back()).lexically_normal();
            for (const auto& file : library_sources) {
                TranslationUnit unit;
                unit.source = state.is_external
                    ? (displayed_compiled_root / file.lexically_relative(compiled_root)).lexically_normal()
                    : file;
                const auto relative = file.lexically_relative(compiled_root);
                unit.path = (state.is_external
                                 ? target.logical_dir / relative
                                 : file.lexically_relative(state.root))
                                .lexically_normal()
                                .string();
                target.sources.push_back(std::move(unit));
            }
        }

        if (!target.sketches.empty()) {
            bool has_main = false;
            for (const auto& source : target.sources) {
                if (std::filesystem::path(source.path).filename() == "main.cpp") {
                    has_main = true;
                    break;
                }
            }
            if (!has_main) {
                if (!push_source("main.cpp", true)) return;
            }
        }
    }

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

bool definition_scalar(const mm::mdy::MDYDocument& doc, std::string_view key,
                       const std::filesystem::path& manifest, std::string& value,
                       std::string_view tool, bool required = true) {
    const auto* values = lookup(doc, key);
    if (values == nullptr) {
        if (!required) {
            value.clear();
            return true;
        }
        std::cerr << tool << ": " << manifest.string() << ": requires one " << key << "\n";
        return false;
    }
    if (values->size() != 1 || values->front().empty()) {
        std::cerr << tool << ": " << manifest.string() << ": requires one non-empty " << key
                  << "\n";
        return false;
    }
    value = values->front();
    return true;
}

std::optional<mm::configure::Responsibility> parse_responsibility(std::string_view value) {
    using R = mm::configure::Responsibility;
    if (value == "reset-vector") return R::ResetVector;
    if (value == "initial-stack") return R::InitialStack;
    if (value == "memory-layout") return R::MemoryLayout;
    if (value == "runtime-init") return R::RuntimeInit;
    if (value == "syscalls") return R::Syscalls;
    return std::nullopt;
}

bool definition_responsibilities(const mm::mdy::MDYDocument& doc,
                                 const std::filesystem::path& manifest,
                                 std::vector<mm::configure::Responsibility>& result,
                                 std::string_view tool) {
    std::set<mm::configure::Responsibility> seen;
    for (const auto& value : all(doc, "provides")) {
        const auto responsibility = parse_responsibility(value);
        if (!responsibility) {
            std::cerr << tool << ": " << manifest.string()
                      << ": unknown responsibility: " << value << "\n";
            return false;
        }
        if (!seen.insert(*responsibility).second) {
            std::cerr << tool << ": " << manifest.string()
                      << ": duplicate provides: " << value << "\n";
            return false;
        }
        result.push_back(*responsibility);
    }
    return true;
}

// Grammar and per-owner uniqueness only. Whether the names resolve is settled
// once the whole tree is collected, by resolve_platform_providers below.
bool definition_providers(const mm::mdy::MDYDocument& doc,
                          const std::filesystem::path& manifest,
                          std::vector<PlatformProviderBinding>& providers,
                          std::string_view tool) {
    for (const auto& value : all(doc, "platform-provider")) {
        std::istringstream fields(value);
        PlatformProviderBinding binding;
        std::string extra;
        if (!(fields >> binding.interface_module >> binding.provider_module) ||
            (fields >> extra)) {
            std::cerr << tool << ": " << manifest.string()
                      << ": platform-provider takes one interface module and one provider "
                         "module: "
                      << value << "\n";
            return false;
        }
        for (const auto& declared : providers) {
            if (declared.interface_module != binding.interface_module) continue;
            std::cerr << tool << ": " << manifest.string()
                      << ": platform-provider declares " << binding.interface_module
                      << " more than once\n";
            return false;
        }
        providers.push_back(std::move(binding));
    }
    return true;
}

bool definition_path(const std::filesystem::path& root, const ManifestNode& node,
                     std::string_view raw, std::filesystem::path& result,
                     std::string_view key, std::string_view tool) {
    const std::filesystem::path relative(raw);
    const auto joined = node.dir / relative;
    if (!is_safe_relative_path(relative, joined)) {
        std::cerr << tool << ": " << node.manifest.string() << ": unsafe " << key << ": "
                  << raw << "\n";
        return false;
    }
    result = joined.lexically_normal();
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    const auto canonical_path = std::filesystem::weakly_canonical(root / result, ec);
    const auto inside = canonical_path.lexically_relative(canonical_root);
    if (ec || inside.empty() || *inside.begin() == ".." ||
        !std::filesystem::is_regular_file(canonical_path, ec) || ec) {
        std::cerr << tool << ": " << node.manifest.string() << ": " << key
                  << " is not a project file: " << raw << "\n";
        return false;
    }
    return true;
}

std::filesystem::path absolute_from_root(const std::filesystem::path& root,
                                        const std::filesystem::path& path) {
    return (path.is_absolute() ? path : root / path).lexically_normal();
}

std::filesystem::path relative_to_root(const std::filesystem::path& root,
                                      const std::filesystem::path& path) {
    return absolute_from_root(root, path).lexically_relative(root.lexically_normal());
}

bool observe_checkout(const std::filesystem::path& path, bool& present,
                      const std::filesystem::path& manifest, std::string_view tool) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        std::cerr << tool << ": " << manifest.string() << ": cannot inspect library source "
                  << path.string() << ": " << ec.message() << "\n";
        return false;
    }
    if (!exists) {
        present = false;
        return true;
    }
    const bool directory = std::filesystem::is_directory(path, ec);
    if (ec) {
        std::cerr << tool << ": " << manifest.string() << ": cannot inspect library source "
                  << path.string() << ": " << ec.message() << "\n";
        return false;
    }
    if (!directory) {
        present = false;
        return true;
    }
    const auto begin = std::filesystem::directory_iterator(path, ec);
    if (ec) {
        std::cerr << tool << ": " << manifest.string() << ": cannot read library source "
                  << path.string() << ": " << ec.message() << "\n";
        return false;
    }
    present = begin != std::filesystem::directory_iterator{};
    return true;
}

bool link_input_name(std::string_view value) {
    if (value.empty()) return false;
    const auto first_valid = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    const auto rest_valid = [&](char c) {
        return first_valid(c) || c == '.' || c == '+' || c == '-';
    };
    if (!first_valid(value.front())) return false;
    for (const auto c : value.substr(1))
        if (!rest_valid(c)) return false;
    return true;
}

bool library_interface_paths(const std::filesystem::path& root, const ManifestNode& node,
                             const mm::mdy::MDYDocument& doc, LibraryDefinition& library,
                             std::string_view tool) {
    const auto add_paths = [&](std::string_view key, std::vector<LibraryPath>& destination) {
        for (const auto& value : all(doc, key)) {
            const std::filesystem::path raw(value);
            const auto normalized = raw.lexically_normal();
            if (raw.empty() || raw.is_absolute() || normalized.empty() ||
                *normalized.begin() == "..") {
                std::cerr << tool << ": " << node.manifest.string() << ": unsafe " << key
                          << ": " << value << "\n";
                return false;
            }
            destination.push_back(
                {LibraryPathBase::Source, (library.source / normalized).lexically_normal()});
        }
        return true;
    };

    if (!add_paths("include-directory", library.include_directories) ||
        !add_paths("library-directory", library.library_directories) ||
        !add_paths("link-archive", library.link_archives))
        return false;

    for (const auto& value : all(doc, "link-input")) {
        if (!link_input_name(value)) {
            std::cerr << tool << ": " << node.manifest.string() << ": invalid link-input: "
                      << value << " (expected [A-Za-z0-9_][A-Za-z0-9_.+-]*)\n";
            return false;
        }
        library.link_inputs.push_back(value);
    }

    if (!library.checkout_present) return true;

    std::error_code ec;
    const auto source = std::filesystem::weakly_canonical(
        absolute_from_root(root, library.source), ec);
    if (ec) {
        std::cerr << tool << ": " << node.manifest.string()
                  << ": cannot resolve library source: " << ec.message() << "\n";
        return false;
    }
    for (const auto* paths : {&library.include_directories, &library.library_directories,
                              &library.link_archives}) {
        for (const auto& entry : *paths) {
            const auto candidate = absolute_from_root(root, entry.path);
            const bool exists = std::filesystem::exists(candidate, ec);
            if (ec) {
                std::cerr << tool << ": " << node.manifest.string()
                          << ": cannot inspect library interface path " << candidate.string()
                          << ": " << ec.message() << "\n";
                return false;
            }
            if (!exists) continue;
            const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
            if (ec || !path_within(source, canonical)) {
                std::cerr << tool << ": " << node.manifest.string()
                          << ": library interface path escapes source: "
                          << entry.path.string() << "\n";
                return false;
            }
        }
    }
    const auto licence = std::filesystem::weakly_canonical(
        absolute_from_root(root, library.licence), ec);
    if (ec || path_within(source, licence)) {
        std::cerr << tool << ": " << node.manifest.string()
                  << ": licence must resolve outside library source\n";
        return false;
    }
    return true;
}

bool resolve_board_chains(Project& project, const LoadPolicy& policy) {
    std::map<std::string_view, std::size_t> board_indices;
    for (std::size_t i = 0; i < project.boards.size(); ++i) {
        board_indices[project.boards[i].name] = i;
    }

    std::vector<std::vector<std::size_t>> chains(project.boards.size());
    for (std::size_t i = 0; i < project.boards.size(); ++i) {
        std::vector<std::size_t> chain{i};
        std::set<std::size_t> in_chain{i};
        std::size_t current = i;
        while (!project.boards[current].derives_from.empty()) {
            const auto& base_name = project.boards[current].derives_from;
            const auto it = board_indices.find(base_name);
            if (it == board_indices.end()) {
                std::cerr << policy.tool << ": " << project.boards[current].manifest.string()
                          << ": derives-from references unknown board: " << base_name;
                if (!project.boards.empty()) {
                    std::cerr << " (available:";
                    for (const auto& b : project.boards) std::cerr << " " << b.name;
                    std::cerr << ")";
                }
                std::cerr << "\n";
                return false;
            }
            const std::size_t base_idx = it->second;
            if (in_chain.contains(base_idx)) {
                std::cerr << policy.tool << ": derives-from: cycle in the board chain:\n";
                auto start_it = std::find(chain.begin(), chain.end(), base_idx);
                for (auto pit = start_it; pit != chain.end(); ++pit) {
                    std::cerr << "    " << project.boards[*pit].manifest.string() << "\n";
                }
                std::cerr << "    " << project.boards[base_idx].manifest.string() << "  <- repeats\n";
                return false;
            }
            chain.push_back(base_idx);
            in_chain.insert(base_idx);
            current = base_idx;
        }
        chains[i] = std::move(chain);
    }

    const char* const immutable_keys[] = {
        "sdk", "cpu", "instruction-set", "float-abi", "security-domain"
    };
    for (std::size_t i = 0; i < project.boards.size(); ++i) {
        if (chains[i].size() <= 1) continue;
        const auto& derived = project.boards[i];
        const auto& supplier = project.boards[chains[i].back()];
        const auto& doc = project.documents[derived.node];
        for (const char* key : immutable_keys) {
            if (lookup(doc, key) != nullptr) {
                std::cerr << policy.tool << ": " << derived.manifest.string()
                          << ": cannot redeclare " << key << " inherited from "
                          << supplier.manifest.string() << "\n";
                return false;
            }
        }
    }

    for (std::size_t i = 0; i < project.boards.size(); ++i) {
        if (chains[i].size() <= 1) continue;
        std::map<mm::configure::Responsibility, std::size_t> seen_responsibilities;
        for (auto it = chains[i].rbegin(); it != chains[i].rend(); ++it) {
            const std::size_t board_idx = *it;
            for (const auto r : project.boards[board_idx].declared_provides) {
                const auto prev = seen_responsibilities.find(r);
                if (prev != seen_responsibilities.end()) {
                    const auto& first_manifest = project.boards[prev->second].manifest;
                    const auto& second_manifest = project.boards[board_idx].manifest;
                    std::cerr << policy.tool << ": responsibility "
                              << mm::configure::responsibility_name(r)
                              << " is provided by both " << first_manifest.string()
                              << " and " << second_manifest.string() << "\n";
                    return false;
                }
                seen_responsibilities[r] = board_idx;
            }
        }
    }

    for (std::size_t i = 0; i < project.boards.size(); ++i) {
        auto& board = project.boards[i];
        const auto& chain_indices = chains[i];
        board.chain.clear();
        for (std::size_t idx : chain_indices) {
            board.chain.push_back(project.boards[idx].name);
        }

        if (chain_indices.size() <= 1) continue;

        const auto& root_base = project.boards[chain_indices.back()];
        board.sdk = root_base.sdk;
        board.cpu = root_base.cpu;
        board.instruction_set = root_base.instruction_set;
        board.float_abi = root_base.float_abi;
        board.security_domain = root_base.security_domain;

        std::string effective_machine;
        for (auto it = chain_indices.rbegin(); it != chain_indices.rend(); ++it) {
            if (!project.boards[*it].machine.empty()) {
                effective_machine = project.boards[*it].machine;
            }
        }
        board.machine = effective_machine;

        std::filesystem::path effective_linker_script;
        for (auto it = chain_indices.rbegin(); it != chain_indices.rend(); ++it) {
            if (!project.boards[*it].linker_script.empty()) {
                effective_linker_script = project.boards[*it].linker_script;
            }
        }
        board.linker_script = effective_linker_script;

        board.sources.clear();
        for (auto it = chain_indices.rbegin(); it != chain_indices.rend(); ++it) {
            const auto& src_list = project.boards[*it].declared_sources;
            board.sources.insert(board.sources.end(), src_list.begin(), src_list.end());
        }

        board.provides.clear();
        for (auto it = chain_indices.rbegin(); it != chain_indices.rend(); ++it) {
            const auto& prov_list = project.boards[*it].declared_provides;
            board.provides.insert(board.provides.end(), prov_list.begin(), prov_list.end());
        }

        board.providers.clear();
        for (auto it = chain_indices.rbegin(); it != chain_indices.rend(); ++it) {
            for (const auto& binding : project.boards[*it].declared_providers) {
                auto pit = std::find_if(board.providers.begin(), board.providers.end(),
                                        [&](const PlatformProviderBinding& existing) {
                                            return existing.interface_module == binding.interface_module;
                                        });
                if (pit != board.providers.end()) {
                    *pit = binding;
                } else {
                    board.providers.push_back(binding);
                }
            }
        }
    }
    return true;
}

bool parse_definitions(Project& project, const std::filesystem::path& root,
                       const LoadPolicy& policy) {
    std::map<std::string, std::filesystem::path, std::less<>> sdk_names;
    std::map<std::string, std::filesystem::path, std::less<>> board_names;
    std::map<std::string, std::filesystem::path, std::less<>> library_names;

    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        const auto& doc = project.documents[i];
        if (node.kind == "sdk") {
            SdkDefinition sdk;
            sdk.node = i;
            sdk.name = node.name;
            sdk.manifest = node.manifest;
            std::string family;
            std::string runtime;
            std::string sysroot;
            std::string runtime_prefix;
            if (!definition_scalar(doc, "target", node.manifest, sdk.target, policy.tool) ||
                !definition_scalar(doc, "compiler-family", node.manifest, family, policy.tool) ||
                !definition_scalar(doc, "runtime", node.manifest, runtime, policy.tool) ||
                !definition_scalar(doc, "specs-profile", node.manifest, sdk.specs_profile,
                                   policy.tool, false) ||
                !definition_scalar(doc, "library", node.manifest, sdk.library,
                                   policy.tool, false) ||
                !definition_scalar(doc, "sysroot", node.manifest, sysroot,
                                   policy.tool, false) ||
                !definition_scalar(doc, "runtime-prefix", node.manifest, runtime_prefix,
                                   policy.tool, false))
                return false;
            if (family == "gcc") sdk.family = CompilerFamily::Gcc;
            else if (family == "clang") sdk.family = CompilerFamily::Clang;
            else if (family == "any") sdk.family_agnostic = true;
            else {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": compiler-family must be gcc, clang, or any\n";
                return false;
            }
            if (runtime == "glibc") sdk.runtime = mm::configure::PlatformRuntime::Glibc;
            else if (runtime == "newlib") sdk.runtime = mm::configure::PlatformRuntime::Newlib;
            else if (runtime == "picolibc") sdk.runtime = mm::configure::PlatformRuntime::Picolibc;
            else if (runtime == "none") sdk.runtime = mm::configure::PlatformRuntime::None;
            else {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": unsupported runtime: " << runtime << "\n";
                return false;
            }
            const auto sdk_system = mm::configure::target_system(sdk.target);
            if (!sdk_system) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": unsupported target: " << sdk.target << "\n";
                return false;
            }
            // A bare-metal SDK supplies the specs, the startup, and the link,
            // and every one of those is family-specific. Only a hosted SDK,
            // which supplies none of them, may decline to name a family.
            if (sdk.family_agnostic &&
                *sdk_system != mm::configure::PlatformSystem::Linux) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": compiler-family: any requires a hosted target\n";
                return false;
            }
            const auto* specs_file_values = lookup(doc, "specs-file");
            if (!sdk.specs_profile.empty() && specs_file_values != nullptr) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": specs-profile and specs-file are mutually exclusive\n";
                return false;
            }
            if (specs_file_values != nullptr) {
                std::string specs_file;
                if (!definition_scalar(doc, "specs-file", node.manifest, specs_file, policy.tool) ||
                    !definition_path(root, node, specs_file, sdk.specs_file, "specs-file", policy.tool))
                    return false;
            }
            for (const auto& pair : {std::pair{std::string_view("sysroot"), &sysroot},
                                     std::pair{std::string_view("runtime-prefix"), &runtime_prefix}}) {
                if (pair.second->empty()) continue;
                std::filesystem::path path(*pair.second);
                if (!path.is_absolute()) {
                    std::cerr << policy.tool << ": " << node.manifest.string() << ": "
                              << pair.first << " must be absolute\n";
                    return false;
                }
                if (pair.first == "sysroot") sdk.sysroot = path;
                else sdk.runtime_prefix = path;
            }
            if (!definition_responsibilities(doc, node.manifest, sdk.provides, policy.tool) ||
                !definition_providers(doc, node.manifest, sdk.providers, policy.tool))
                return false;
            const bool hosted = *mm::configure::target_system(sdk.target) ==
                                mm::configure::PlatformSystem::Linux;
            if (hosted && !sdk.provides.empty()) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": hosted SDK cannot declare provides\n";
                return false;
            }
            if (!sdk.specs_profile.empty() && !sdk.provides.empty()) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": specs-profile SDK cannot declare provides\n";
                return false;
            }
            if (!sdk.specs_file.empty()) {
                std::set<mm::configure::Responsibility> supplied(sdk.provides.begin(), sdk.provides.end());
                if (!supplied.contains(mm::configure::Responsibility::RuntimeInit) ||
                    !supplied.contains(mm::configure::Responsibility::Syscalls) || supplied.size() != 2) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": specs-file SDK must provide runtime-init and syscalls\n";
                    return false;
                }
            }
            if (!sdk.specs_profile.empty() &&
                !(sdk.family == CompilerFamily::Gcc && sdk.target == "arm-none-eabi" &&
                  sdk.specs_profile == "rdimon")) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": unknown specs profile: " << sdk.specs_profile << "\n";
                return false;
            }
            const auto duplicate = sdk_names.find(sdk.name);
            if (duplicate != sdk_names.end()) {
                std::cerr << policy.tool << ": SDK name \"" << sdk.name
                          << "\" is declared by both " << duplicate->second.string() << " and "
                          << sdk.manifest.string() << "\n";
                return false;
            }
            sdk_names[sdk.name] = sdk.manifest;
            project.sdks.push_back(std::move(sdk));
        } else if (node.kind == "board") {
            BoardDefinition board;
            board.node = i;
            board.name = node.name;
            board.manifest = node.manifest;
            if (!definition_scalar(doc, "derives-from", node.manifest, board.derives_from,
                                   policy.tool, false))
                return false;

            std::string linker;
            if (board.derives_from.empty()) {
                if (!definition_scalar(doc, "sdk", node.manifest, board.sdk, policy.tool) ||
                    !definition_scalar(doc, "cpu", node.manifest, board.cpu, policy.tool) ||
                    !definition_scalar(doc, "instruction-set", node.manifest,
                                       board.instruction_set, policy.tool) ||
                    !definition_scalar(doc, "float-abi", node.manifest, board.float_abi,
                                       policy.tool) ||
                    !definition_scalar(doc, "security-domain", node.manifest,
                                       board.security_domain, policy.tool, false) ||
                    !definition_scalar(doc, "machine", node.manifest, board.machine,
                                       policy.tool, false) ||
                    !definition_scalar(doc, "linker-script", node.manifest, linker,
                                       policy.tool, false))
                    return false;
                if (board.security_domain.empty()) board.security_domain = "non-secure";
                if (board.security_domain != "secure" && board.security_domain != "non-secure") {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": security-domain must be secure or non-secure\n";
                    return false;
                }
            } else {
                if (!definition_scalar(doc, "sdk", node.manifest, board.sdk, policy.tool, false) ||
                    !definition_scalar(doc, "cpu", node.manifest, board.cpu, policy.tool, false) ||
                    !definition_scalar(doc, "instruction-set", node.manifest,
                                       board.instruction_set, policy.tool, false) ||
                    !definition_scalar(doc, "float-abi", node.manifest, board.float_abi,
                                       policy.tool, false) ||
                    !definition_scalar(doc, "security-domain", node.manifest,
                                       board.security_domain, policy.tool, false) ||
                    !definition_scalar(doc, "machine", node.manifest, board.machine,
                                       policy.tool, false) ||
                    !definition_scalar(doc, "linker-script", node.manifest, linker,
                                       policy.tool, false))
                    return false;
                if (!board.security_domain.empty() &&
                    board.security_domain != "secure" && board.security_domain != "non-secure") {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": security-domain must be secure or non-secure\n";
                    return false;
                }
            }
            if (!linker.empty() &&
                !definition_path(root, node, linker, board.linker_script,
                                 "linker-script", policy.tool))
                return false;
            for (const auto& source : all(doc, "file")) {
                std::filesystem::path path;
                if (!definition_path(root, node, source, path, "file", policy.tool)) return false;
                board.sources.push_back(std::move(path));
            }
            if (!definition_responsibilities(doc, node.manifest, board.provides, policy.tool) ||
                !definition_providers(doc, node.manifest, board.providers, policy.tool))
                return false;

            board.declared_linker_script = board.linker_script;
            board.declared_sources = board.sources;
            board.declared_provides = board.provides;
            board.declared_providers = board.providers;

            const auto duplicate = board_names.find(board.name);
            if (duplicate != board_names.end()) {
                std::cerr << policy.tool << ": board name \"" << board.name
                          << "\" is declared by both " << duplicate->second.string() << " and "
                          << board.manifest.string() << "\n";
                return false;
            }
            board_names[board.name] = board.manifest;
            project.boards.push_back(std::move(board));
        } else if (node.kind == "library") {
            LibraryDefinition library;
            library.node = i;
            library.name = node.name;
            library.manifest = node.manifest;
            std::string source;
            std::string licence;
            if (!definition_scalar(doc, "source", node.manifest, source, policy.tool) ||
                !definition_scalar(doc, "licence", node.manifest, licence, policy.tool))
                return false;

            const auto source_path = (node.dir / source).lexically_normal();
            const auto absolute_source = absolute_from_root(root, source_path);
            if (std::filesystem::path(source).is_absolute() ||
                !path_contained_in(root, absolute_source)) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": source is outside the project: " << source << "\n";
                return false;
            }
            std::error_code source_ec;
            const auto canonical_source = std::filesystem::weakly_canonical(
                absolute_source, source_ec);
            if (source_ec) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": cannot resolve library source: " << source_ec.message() << "\n";
                return false;
            }
            library.source = relative_to_root(root, canonical_source);

            std::filesystem::path licence_path;
            if (!definition_path(root, node, licence, licence_path, "licence", policy.tool))
                return false;
            library.licence = relative_to_root(root, licence_path);
            if (path_within(absolute_source, absolute_from_root(root, library.licence))) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": licence must be outside library source\n";
                return false;
            }

            if (!definition_scalar(doc, "external-build", node.manifest,
                                   library.external_build, policy.tool, false))
                return false;
            if (!library.external_build.empty()) {
                if (library.external_build != "cmake") {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": unknown external-build: " << library.external_build << "\n";
                    return false;
                }
                const auto cmake_dir = (node.dir / "cmake").lexically_normal();
                const auto absolute_cmake = absolute_from_root(root, cmake_dir);
                std::error_code ec;
                const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
                if (ec) return false;
                const auto canonical_cmake = std::filesystem::weakly_canonical(absolute_cmake, ec);
                if (ec || !path_within(canonical_root, canonical_cmake)) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": cmake directory is outside the project: "
                              << cmake_dir.generic_string() << "\n";
                    return false;
                }
                const bool cmake_is_dir = std::filesystem::is_directory(canonical_cmake, ec);
                if (ec || !cmake_is_dir) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": external-build library requires a cmake directory beside its manifest\n";
                    return false;
                }
                const auto cmakelists = absolute_cmake / "CMakeLists.txt";
                const auto canonical_cmakelists = std::filesystem::weakly_canonical(cmakelists, ec);
                const bool cmakelists_is_file = std::filesystem::is_regular_file(canonical_cmakelists, ec);
                if (ec || !path_within(canonical_root, canonical_cmakelists) || !cmakelists_is_file) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": external-build library requires cmake/CMakeLists.txt\n";
                    return false;
                }
                const auto canonical_source = std::filesystem::weakly_canonical(absolute_source, ec);
                if (ec || path_within(canonical_source, canonical_cmake) ||
                    path_within(canonical_source, canonical_cmakelists)) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": cmake directory must resolve outside library source\n";
                    return false;
                }
            }

            if (!observe_checkout(absolute_source, library.checkout_present,
                                  node.manifest, policy.tool) ||
                !library_interface_paths(root, node, doc, library, policy.tool))
                return false;

            const auto duplicate = library_names.find(library.name);
            if (duplicate != library_names.end()) {
                std::cerr << policy.tool << ": library name \"" << library.name
                          << "\" is declared by both " << duplicate->second.string() << " and "
                          << library.manifest.string() << "\n";
                return false;
            }
            library_names[library.name] = library.manifest;
            project.libraries.push_back(std::move(library));
        }
    }

    for (const auto& sdk : project.sdks) {
        if (sdk.library.empty()) continue;
        const LibraryDefinition* library = nullptr;
        for (const auto& candidate : project.libraries)
            if (candidate.name == sdk.library) library = &candidate;
        if (library == nullptr) {
            std::cerr << policy.tool << ": " << sdk.manifest.string()
                      << ": SDK references unknown library: " << sdk.library;
            if (!project.libraries.empty()) {
                std::cerr << " (available:";
                for (const auto& candidate_lib : project.libraries) std::cerr << " " << candidate_lib.name;
                std::cerr << ")";
            }
            std::cerr << "\n";
            return false;
        }
        if (!library->external_build.empty()) {
            if (!sdk.specs_profile.empty()) {
                std::cerr << policy.tool << ": " << sdk.manifest.string()
                          << ": SDK whose library declares external-build cannot declare specs-profile\n";
                return false;
            }
            if (!sdk.specs_file.empty()) {
                std::cerr << policy.tool << ": " << sdk.manifest.string()
                          << ": SDK whose library declares external-build cannot declare specs-file\n";
                return false;
            }
        }
    }

    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        if (node.kind != "module") continue;

        auto& target = project.targets[project.target[i]];
        if (!definition_scalar(project.documents[i], "library", node.manifest,
                               target.library, policy.tool, false))
            return false;
        if (target.library.empty()) continue;

        // A wrapper takes lib., and a platform provider takes platform. A
        // provider that names a library is still not a wrapper:
        // docs/modules-c++20.mdy already draws that line for a provider over a
        // bridge ABI, on the grounds that its public dependency is the project
        // interface module rather than a foreign library API. The same is true
        // of one that reaches its library directly.
        if (!target.module_name.starts_with("lib.") &&
            !target.module_name.starts_with("platform.")) {
            std::cerr << policy.tool << ": " << node.manifest.string()
                      << ": module naming a library must use the lib. or "
                         "platform. prefix: "
                      << target.module_name << "\n";
            return false;
        }

        bool found = false;
        for (const auto& library : project.libraries)
            if (library.name == target.library) found = true;
        if (found) continue;

        std::cerr << policy.tool << ": " << node.manifest.string()
                  << ": module references unknown library: " << target.library;
        if (!project.libraries.empty()) {
            std::cerr << " (available:";
            for (const auto& library : project.libraries) std::cerr << " " << library.name;
            std::cerr << ")";
        }
        std::cerr << "\n";
        return false;
    }

    if (!resolve_board_chains(project, policy)) return false;

    for (auto& board : project.boards) {
        const SdkDefinition* sdk = nullptr;
        for (const auto& candidate : project.sdks)
            if (candidate.name == board.sdk) sdk = &candidate;
        if (sdk == nullptr) {
            std::cerr << policy.tool << ": " << board.manifest.string()
                      << ": board references unknown SDK: " << board.sdk;
            if (board.chain.size() > 1) {
                for (const auto& b : project.boards) {
                    if (b.name == board.chain.back()) {
                        std::cerr << " (inherited from " << b.manifest.string() << ")";
                        break;
                    }
                }
            }
            std::cerr << "\n";
            return false;
        }
        const auto* processor = find_processor_entry(sdk->family, sdk->target, board.cpu,
                                                     board.instruction_set, board.float_abi,
                                                     board.security_domain);
        if (processor == nullptr) {
            std::cerr << policy.tool << ": " << board.manifest.string()
                      << ": unknown processor combination for SDK " << sdk->name;
            if (board.chain.size() > 1) {
                for (const auto& b : project.boards) {
                    if (b.name == board.chain.back()) {
                        std::cerr << " (inherited from " << b.manifest.string() << ")";
                        break;
                    }
                }
            }
            std::cerr << "\n";
            return false;
        }
        for (std::size_t i = 0; i < processor_argument_count(*processor); ++i)
            board.compiler_arguments.emplace_back(processor->arguments[i]);
        const LibraryDefinition* library = nullptr;
        if (!sdk->library.empty()) {
            for (const auto& candidate : project.libraries)
                if (candidate.name == sdk->library) library = &candidate;
        }
        const bool external = library != nullptr && !library->external_build.empty();
        const bool bare_metal = mm::configure::target_system(sdk->target) ==
                                mm::configure::PlatformSystem::BareMetal;
        if (!bare_metal) {
            if (!board.linker_script.empty()) {
                std::cerr << policy.tool << ": " << board.manifest.string()
                          << ": hosted board cannot declare linker-script\n";
                return false;
            }
            if (!board.provides.empty()) {
                std::cerr << policy.tool << ": " << board.manifest.string()
                          << ": hosted board cannot declare provides\n";
                return false;
            }
        } else if (external) {
            if (!board.linker_script.empty()) {
                const BoardDefinition* declaring = &board;
                for (const auto& ancestor_name : board.chain) {
                    for (const auto& b : project.boards) {
                        if (b.name == ancestor_name && !b.declared_linker_script.empty()) {
                            declaring = &b;
                            break;
                        }
                    }
                    if (!declaring->declared_linker_script.empty()) break;
                }
                std::cerr << policy.tool << ": " << declaring->manifest.string()
                          << ": board whose SDK \"" << sdk->name
                          << "\" owns an external link cannot declare linker-script\n";
                return false;
            }
        } else {
            if (board.linker_script.empty()) {
                std::cerr << policy.tool << ": " << board.manifest.string()
                          << ": board requires linker-script";
                if (board.chain.size() > 1) {
                    for (const auto& b : project.boards) {
                        if (b.name == board.chain[1]) {
                            std::cerr << " (none inherited from " << b.manifest.string() << ")";
                            break;
                        }
                    }
                }
                std::cerr << "\n";
                return false;
            }
            if (board.sources.empty()) {
                std::cerr << policy.tool << ": " << board.manifest.string()
                          << ": board requires at least one file";
                if (board.chain.size() > 1) {
                    for (const auto& b : project.boards) {
                        if (b.name == board.chain[1]) {
                            std::cerr << " (none inherited from " << b.manifest.string() << ")";
                            break;
                        }
                    }
                }
                std::cerr << "\n";
                return false;
            }
        }
    }
    return true;
}

std::map<std::string, std::size_t, std::less<>> modules_by_module_name(const Project& project) {
    std::map<std::string, std::size_t, std::less<>> modules;
    for (std::size_t i = 0; i < project.targets.size(); ++i)
        if (project.targets[i].kind == "module")
            modules.emplace(project.targets[i].module_name, i);
    return modules;
}

// Walks authored use: edges only. An unresolved name is left to order(), which
// is where an unknown module is already diagnosed against a built tree.
bool reaches_module(const Project& project,
                    const std::map<std::string, std::size_t, std::less<>>& modules,
                    std::size_t start, std::string_view wanted) {
    std::vector<std::size_t> pending{start};
    std::set<std::size_t> seen;
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        if (!seen.insert(index).second) continue;
        for (const auto& used : project.targets[index].uses) {
            if (used == wanted) return true;
            const auto entry = modules.find(used);
            if (entry != modules.end()) pending.push_back(entry->second);
        }
    }
    return false;
}

// Resolved after the complete manifest tree is collected, so declaration order
// and folder order are both irrelevant, and after the duplicate module-name
// check, so one name means one module here.
bool resolve_platform_providers(const Project& project, const LoadPolicy& policy) {
    const auto modules = modules_by_module_name(project);

    std::vector<std::string_view> interfaces;
    for (const auto& target : project.targets)
        if (target.kind == "module" && target.platform_interface)
            interfaces.push_back(target.module_name);

    bool ok = true;
    const auto resolve = [&](const std::filesystem::path& manifest,
                             const std::vector<PlatformProviderBinding>& providers) {
        for (const auto& binding : providers) {
            const auto declared = modules.find(binding.interface_module);
            if (declared == modules.end() ||
                !project.targets[declared->second].platform_interface) {
                std::cerr << policy.tool << ": " << manifest.string()
                          << ": platform-provider names " << binding.interface_module
                          << ", which is not a platform interface";
                if (!interfaces.empty()) {
                    std::cerr << " (available:";
                    for (const auto& candidate : interfaces) std::cerr << " " << candidate;
                    std::cerr << ")";
                }
                std::cerr << "\n";
                ok = false;
                continue;
            }
            const auto provider = modules.find(binding.provider_module);
            if (provider == modules.end()) {
                std::cerr << policy.tool << ": " << manifest.string()
                          << ": platform-provider names unknown provider module: "
                          << binding.provider_module << "\n";
                ok = false;
                continue;
            }
            if (provider->second == declared->second) {
                std::cerr << policy.tool << ": " << manifest.string()
                          << ": platform-provider binds " << binding.interface_module
                          << " to itself\n";
                ok = false;
                continue;
            }
            if (!reaches_module(project, modules, provider->second, binding.interface_module)) {
                std::cerr << policy.tool << ": " << manifest.string() << ": provider "
                          << binding.provider_module << " does not use "
                          << binding.interface_module << "\n";
                ok = false;
            }
        }
    };

    for (const auto& sdk : project.sdks) resolve(sdk.manifest, sdk.providers);
    for (const auto& board : project.boards) resolve(board.manifest, board.providers);
    return ok;
}
bool is_safe_board_name(std::string_view name) {
    if (name.empty()) return false;
    const auto is_lead = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    const auto is_rest = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '+' || c == '-';
    };
    if (!is_lead(name.front())) return false;
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!is_rest(name[i])) return false;
    }
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

ResolvedRoots resolve_roots(const std::filesystem::path& manifest_or_dir) {
    ResolvedRoots result;
    const auto manifest_path = resolve_manifest(manifest_or_dir);
    if (manifest_path.filename() != "mm.mdy" || !safe_exists(manifest_path)) {
        result.ok = false;
        return result;
    }

    std::error_code ec;
    result.requested_manifest =
        std::filesystem::weakly_canonical(manifest_path, ec);
    if (ec) {
        result.ok = false;
        return result;
    }
    result.requested_dir = result.requested_manifest.parent_path();

    const auto req_doc = mm::mdy::Parser::parse_file(result.requested_manifest);
    result.requested_node = first(req_doc, "name");

    std::filesystem::path external_dir;
    std::string project_val;

    for (auto dir = result.requested_dir; !dir.empty();
         dir = dir.parent_path()) {
        const auto candidate = dir / "mm.mdy";
        if (safe_exists(candidate)) {
            const auto doc = mm::mdy::Parser::parse_file(candidate);
            const auto kind = first(doc, "kind");
            const auto proj = first(doc, "project");
            if (!proj.empty()) {
                external_dir = dir;
                project_val = proj;
                break;
            }
            if (kind == "project") {
                result.project_root =
                    std::filesystem::weakly_canonical(dir, ec);
                if (ec) {
                    result.ok = false;
                    return result;
                }
                result.tools_dir = result.project_root / "out" / "bin";
                return result;
            }
        }
        if (!dir.has_relative_path()) break;
    }

    if (!external_dir.empty()) {
        result.external_root =
            std::filesystem::weakly_canonical(external_dir, ec);
        if (ec) {
            result.ok = false;
            return result;
        }
        const auto resolved_proj =
            (external_dir / project_val).lexically_normal();
        result.project_root =
            std::filesystem::weakly_canonical(resolved_proj, ec);
        if (ec || !safe_exists(result.project_root / "mm.mdy")) {
            result.ok = false;
            return result;
        }
        result.tools_dir = result.project_root / "out" / "bin";
        return result;
    }

    const auto proj_root = find_project_root(result.requested_dir);
    if (proj_root.empty()) {
        result.ok = false;
        return result;
    }
    result.project_root = std::filesystem::weakly_canonical(proj_root, ec);
    if (ec) {
        result.ok = false;
        return result;
    }
    result.tools_dir = result.project_root / "out" / "bin";
    return result;
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

    if (policy.external) {
        state.is_external = true;
        state.external_root =
            std::filesystem::weakly_canonical(*policy.external, ec);
        state.external_root_display = policy.external->lexically_normal();
        if (ec) {
            std::cerr << policy.tool << ": cannot resolve external root "
                      << policy.external->string()
                      << ": " << ec.message() << "\n";
            project.ok = false;
            return project;
        }

        for (auto p = state.external_root.parent_path(); !p.empty();
             p = p.parent_path()) {
            const auto p_manifest = p / "mm.mdy";
            if (safe_exists(p_manifest)) {
                const auto p_doc = mm::mdy::Parser::parse_file(p_manifest);
                if (first(p_doc, "kind") == "project" ||
                    !first(p_doc, "project").empty()) {
                    const auto ext_manifest =
                        (state.external_root / "mm.mdy").lexically_normal();
                    std::cerr << policy.tool << ": " << ext_manifest.string()
                              << ": external root is inside another tree\n";
                    project.ok = false;
                    return project;
                }
            }
            if (!p.has_relative_path()) break;
        }

        walk_project(*policy.external, 0, project, state);
        if (!project.ok) return project;
    }

    if (!parse_definitions(project, state.root, policy)) {
        project.ok = false;
        return project;
    }

    // Checked once the whole tree is built, not incrementally during the
    // walk, since a duplicate can only be found once every candidate name
    // is known. Without this, index_of_module returns whichever target
    // with a given module: name it happens to see first, silently treating
    // two real, different modules as interchangeable; two app: targets with
    // the same name would silently install() one over the other under
    // out/bin; and nothing at all currently rules out two different
    // manifests claiming the same directory.
    std::map<std::string, const BuildableNode*, std::less<>> modules_by_name;
    std::map<std::pair<bool, std::string>, const BuildableNode*> apps_by_name;
    std::map<std::pair<bool, std::filesystem::path>,
             const BuildableNode*> targets_by_dir;

    auto check_dir = [&](const BuildableNode& target) {
        const auto key = std::make_pair(target.external, target.logical_dir);
        const auto it = targets_by_dir.find(key);
        if (it != targets_by_dir.end()) {
            std::cerr << policy.tool << ": " << target.logical_dir.string()
                      << " is declared by more than one manifest: "
                      << it->second->name << " and " << target.name << "\n";
            project.ok = false;
            return;
        }
        targets_by_dir[key] = &target;
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
            const auto key = std::make_pair(target.external, target.name);
            const auto it = apps_by_name.find(key);
            if (it != apps_by_name.end()) {
                std::cerr << policy.tool << ": app name \"" << target.name << "\" is declared by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                project.ok = false;
            } else {
                apps_by_name[key] = &target;
            }
        }
    }

    for (const auto& target : project.tests) check_dir(target);
    for (const auto& target : project.docs) check_dir(target);

    if (project.ok && !resolve_platform_providers(project, policy)) project.ok = false;

    return project;
}
}
