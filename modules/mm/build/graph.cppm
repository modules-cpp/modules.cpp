// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

export module mm.build:graph;

import :compile;
import :config;
import :manifest;
import :platform;

// Topological order and closure algorithms over a tree's use: edges.

export namespace mm::build {

// Topological order over use: edges, dependencies first. False on a cycle or an
// unknown module name.
bool order(const Tree& tree, std::vector<std::size_t>& out);

// The same, restricted to what one target needs: every target reachable from
// index through use:, dependencies first and index itself last. This is how a
// test target builds the modules it uses without building the whole project.
bool order_from(const Tree& tree, std::size_t index, std::vector<std::size_t>& out);

// Objects of a target plus every module reachable through use:, target first.
std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index,
                                           const ArtifactContext& context);
std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index);

// The same, extended with the closure of every selected provider the target's
// own closure requires. Objects are never repeated: a provider required by two
// applications is compiled once and appears once in each link. extra names the
// provider modules that were merged in, for diagnostics.
// reached, when given, receives every target index the closure visited, a
// consumer ahead of the modules it uses. library_link_inputs wants it reversed.
std::vector<std::filesystem::path> augmented_closure(
    const Tree& tree, std::size_t index, const PlatformProviders& providers,
    const ArtifactContext& context, std::vector<std::string>* merged = nullptr,
    std::vector<std::size_t>* reached = nullptr);
std::vector<std::filesystem::path> augmented_closure(
    const Tree& tree, std::size_t index, const PlatformProviders& providers,
    std::vector<std::string>* merged = nullptr,
    std::vector<std::size_t>* reached = nullptr);

// The library segment for a closure: the link-input names of every library a
// reached wrapper declares, once each, a dependency's library after the module
// that needed it. A library whose checkout is absent is reported here rather
// than discovered as an undefined symbol.
// The C objects for a closure: every library a reached module names that
// declares c-source, compiled once per library per run -- compiled keeps
// what was built -- and appended to objects, a dependency's after the module
// that needed it. On a lane an external bridge links, reaching such a library
// is refused naming the module: the bridge owns third-party C there. Answers
// an exit status.
[[nodiscard]] int library_c_objects(
    const std::filesystem::path& project_root,
    const std::vector<LibraryDefinition>& libraries,
    const Tree& tree,
    const std::vector<std::size_t>& reached,
    const Toolchain& toolchain,
    const ArtifactContext& context,
    bool external_link,
    std::map<std::string, std::vector<std::filesystem::path>>& compiled,
    std::vector<std::filesystem::path>& objects,
    std::string_view tool = "build");

[[nodiscard]] bool library_link_inputs(
    const std::filesystem::path& project_root,
    const std::vector<LibraryDefinition>& libraries,
    const Tree& tree,
    const std::vector<std::size_t>& reached,
    std::vector<std::string>& inputs,
    std::string_view tool = "build");

}
