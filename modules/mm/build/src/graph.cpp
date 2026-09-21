// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

module mm.build;

import mm.configure;
import mm.json;
import mm.mdy;
import :detail;
import :manifest;
import :platform;
import :compile;
import :graph;

namespace mm::build {
// This unit implements the mm.build:graph partition: ordering, closures, and link-input computation.
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
                   const ArtifactContext* context,
                   std::vector<bool>& seen, std::vector<std::filesystem::path>& out,
                   std::vector<std::size_t>* reached) {
    if (seen[index]) return;
    seen[index] = true;

    if (context != nullptr) {
        const auto& objs = context->objects(tree.targets[index]);
        if (!objs.empty()) {
            for (const auto& object : objs) out.push_back(object);
        } else {
            for (const auto& object : tree.targets[index].objects)
                out.push_back(object);
        }
    } else {
        for (const auto& object : tree.targets[index].objects)
            out.push_back(object);
    }
    // Recorded before the dependencies, so the caller sees consumers ahead of
    // what they consume. A library segment wants the reverse of that.
    if (reached != nullptr) reached->push_back(index);

    for (const auto& used : tree.targets[index].uses) {
        const auto dependency = index_of_module(tree, used);
        if (dependency != tree.targets.size())
            closure_visit(dependency, tree, context, seen, out, reached);
    }
}

void closure_visit(std::size_t index, const Tree& tree,
                   std::vector<bool>& seen, std::vector<std::filesystem::path>& out,
                   std::vector<std::size_t>* reached) {
    closure_visit(index, tree, nullptr, seen, out, reached);
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

std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index,
                                           const ArtifactContext& context) {
    std::vector<bool> seen(tree.targets.size(), false);
    std::vector<std::filesystem::path> objects;
    closure_visit(index, tree, &context, seen, objects, nullptr);
    return objects;
}

std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index) {
    std::vector<bool> seen(tree.targets.size(), false);
    std::vector<std::filesystem::path> objects;
    closure_visit(index, tree, nullptr, seen, objects, nullptr);
    return objects;
}

bool library_link_inputs(const std::filesystem::path& project_root,
                         const std::vector<LibraryDefinition>& libraries,
                         const Tree& tree,
                         const std::vector<std::size_t>& reached,
                         std::vector<std::string>& inputs,
                         std::string_view tool) {
    inputs.clear();

    std::error_code ec;
    const auto root = std::filesystem::absolute(project_root, ec).lexically_normal();
    if (ec) {
        std::cerr << tool << ": cannot resolve project root " << project_root.string()
                  << ": " << ec.message() << "\n";
        return false;
    }

    // Reverse visit order: closure_visit records a consumer before the modules
    // it uses, and a linker wants the dependency after the thing that needs it.
    for (auto position = reached.rbegin(); position != reached.rend(); ++position) {
        const auto& target = tree.targets[*position];
        if (target.library.empty()) continue;

        const LibraryDefinition* definition = nullptr;
        for (const auto& library : libraries)
            if (library.name == target.library) definition = &library;
        if (definition == nullptr) {
            std::cerr << tool << ": " << (target.dir / "mm.mdy").string()
                      << ": module references unknown library: " << target.library << "\n";
            return false;
        }
        if (!validate_library_checkout(root, *definition, tool)) {
            std::cerr << tool << ": " << (target.dir / "mm.mdy").string()
                      << ": module " << target.name << " cannot use library "
                      << definition->name << "\n";
            return false;
        }

        // One contribution per library however many wrappers reach it: an
        // archive named twice is not the same harmless repetition an include
        // directory is.
        for (const auto& value : definition->link_inputs)
            if (std::find(inputs.begin(), inputs.end(), value) == inputs.end())
                inputs.push_back(value);
    }
    return true;
}

std::vector<std::filesystem::path> augmented_closure(
    const Tree& tree, std::size_t index, const PlatformProviders& providers,
    const ArtifactContext& context, std::vector<std::string>* merged,
    std::vector<std::size_t>* reached) {
    std::vector<bool> seen(tree.targets.size(), false);
    std::vector<std::filesystem::path> objects;
    closure_visit(index, tree, &context, seen, objects, reached);

    // Provider closures can reach further platform interfaces. Rescan to a
    // fixed point: a newly reached interface may precede the first interface
    // in tree order and would be missed by a single forward pass. seen carries
    // across every pass, so shared objects and providers are still merged once.
    bool added = true;
    while (added) {
        added = false;
        for (std::size_t i = 0; i < tree.targets.size(); ++i) {
            if (!seen[i] || !tree.targets[i].platform_interface) continue;
            const auto* binding = providers.binding(tree.targets[i].module_name);
            if (binding == nullptr) continue;
            const auto provider = index_of_module(tree, binding->provider_module);
            if (provider == tree.targets.size() || seen[provider]) continue;
            if (merged != nullptr) merged->push_back(binding->provider_module);
            closure_visit(provider, tree, &context, seen, objects, reached);
            added = true;
        }
    }

    return objects;
}

std::vector<std::filesystem::path> augmented_closure(const Tree& tree, std::size_t index,
                                                     const PlatformProviders& providers,
                                                     std::vector<std::string>* merged,
                                                     std::vector<std::size_t>* reached) {
    std::vector<bool> seen(tree.targets.size(), false);
    std::vector<std::filesystem::path> objects;
    closure_visit(index, tree, nullptr, seen, objects, reached);

    // Provider closures can reach further platform interfaces. Rescan to a
    // fixed point: a newly reached interface may precede the first interface
    // in tree order and would be missed by a single forward pass. seen carries
    // across every pass, so shared objects and providers are still merged once.
    bool added = true;
    while (added) {
        added = false;
        for (std::size_t i = 0; i < tree.targets.size(); ++i) {
            if (!seen[i] || !tree.targets[i].platform_interface) continue;
            const auto* binding = providers.binding(tree.targets[i].module_name);
            if (binding == nullptr) continue;
            const auto provider = index_of_module(tree, binding->provider_module);
            if (provider == tree.targets.size() || seen[provider]) continue;
            if (merged != nullptr) merged->push_back(binding->provider_module);
            closure_visit(provider, tree, nullptr, seen, objects, reached);
            added = true;
        }
    }

    return objects;
}
}
