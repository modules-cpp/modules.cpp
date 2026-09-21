// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module mm.build:platform;

import mm.configure;
import :config;
import :manifest;

// Provider bindings, structural properties, and platform units resolved from a loaded project.

export namespace mm::build {

struct Availability {
    bool available = false;
    std::string reason;
};

// The provider one selected platform binds an interface to, and the definition
// that supplied it. from_board distinguishes a board's specialisation from its
// SDK's default, which the board overrides.
struct EffectiveProvider {
    std::string interface_module;
    std::string provider_module;
    std::string owner;
    bool from_board = false;
};

// The shared post-load, pre-filter provider analysis.
//
// It is computed from the complete Project::targets collection and its globally
// resolved use: names, before either lane tool constructs its filtered tree.
// That ordering is the point: the interface requirement that decides whether a
// node survives filtering cannot be discovered from an already filtered tree.
// build and test consume this one result rather than approximating it twice.
struct PlatformProviders {
    bool ok = true;

    // Every module carrying the platform-interface marker, in walk order.
    std::vector<std::string> interfaces;

    // Named as the provider of some declaration, whether or not selected.
    std::vector<std::string> declared;

    // The effective binding per interface after SDK and board selection.
    std::vector<EffectiveProvider> effective;

    // Interfaces reached by each node's authored use: closure, parallel to
    // Project::nodes and empty for a node that requires none. Authored edges
    // only: an injected provider is never inserted here.
    std::vector<std::vector<std::string>> requirements;

    // The platform this analysis was resolved against, named so a diagnostic
    // can say which SDK and board failed to supply a binding.
    std::string selected_sdk;
    std::string selected_board;

    [[nodiscard]] const EffectiveProvider* binding(std::string_view interface_module) const;
    [[nodiscard]] bool declares_provider(std::string_view module_name) const;
    [[nodiscard]] bool selects_provider(std::string_view module_name) const;

    // Why an executable at this node cannot be linked in the analysed lane, or
    // empty when every interface it reaches resolves.
    [[nodiscard]] std::string unmet_requirement(std::size_t node) const;
};

// Resolves the analysis above for one lane. A host lane binds nothing and uses
// an interface's linkable fallback, which lets unit tests register stand-ins.
// A target lane with no effective binding leaves every reached interface
// requirement unmet, so the executable is unavailable before compilation.
[[nodiscard]] PlatformProviders platform_providers(const Project& project, bool target_lane,
                                                   const Platform* platform,
                                                   std::string_view tool = "build");

// lane_capabilities, when supplied, is parallel to Project::nodes and prevents
// an executable from surviving after its selected provider was capability-
// filtered out of the lane.
[[nodiscard]] Availability availability(const Project& project, std::size_t node,
                                        bool capability, bool target_lane,
                                        const Platform* platform,
                                        const PlatformProviders* providers = nullptr,
                                        const std::vector<bool>* lane_capabilities = nullptr);

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

// Depth first over folder: entries, starting at a kind:project, kind:dir, or kind:library
// manifest. Paths in the result are relative to dir.
Tree load_tree(const std::filesystem::path& dir, const LoadPolicy& policy = {});

// Loads a single kind:test manifest as a target whose sources are its unit:
// entries. Sets ok to false and reports the reason on failure.
BuildableNode load_test(const std::filesystem::path& manifest_path, bool& ok,
                        const LoadPolicy& policy = {});

}
