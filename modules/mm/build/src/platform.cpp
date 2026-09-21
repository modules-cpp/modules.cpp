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
import :platform;
import :compile;
import :graph;

namespace mm::build {
// This unit implements the mm.build:platform partition: structural properties, providers, and platform units.
StructuralProperties structural_properties(
    const std::vector<mm::configure::OptionValues>& resolved) {
    StructuralProperties properties;
    properties.nodes.reserve(resolved.size());
    const auto property = [](const mm::configure::OptionValue& value) {
        return StructuralProperty{value.boolean, value.origin, value.value_source,
                                  value.read_only, value.lock_source};
    };
    for (const auto& values : resolved) {
        properties.nodes.push_back({property(values.find("buildable-host")->second),
                                    property(values.find("buildable-target")->second),
                                    property(values.find("core")->second)});
    }
    return properties;
}

bool validate_structural_properties(
    const std::vector<mm::configure::OptionNode>& nodes,
    const StructuralProperties& properties, std::string_view tool) {
    if (nodes.size() != properties.nodes.size()) return false;

    // use: names a module, and the resolver is indexed by node, so the edge
    // needs a module name to node index map. index_of_module answers with a
    // targets index instead, which is the wrong side of the join here.
    std::map<std::string, std::size_t, std::less<>> modules;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == "module" && !nodes[i].module_name.empty())
            modules.emplace(nodes[i].module_name, i);

    bool ok = true;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind != "sdk" && properties.nodes[i].core.value && !nodes[i].library.empty()) {
            std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                      << nodes[i].name << " is core, but declares library "
                      << nodes[i].library << "\n";
            ok = false;
        }
        for (const auto& used : nodes[i].uses) {
            const auto found = modules.find(used);
            if (found == modules.end()) continue;  // order() reports unknown modules
            for (const auto lane : {false, true}) {
                const auto& consumer = lane ? properties.nodes[i].buildable_target
                                            : properties.nodes[i].buildable_host;
                const auto& dependency = lane ? properties.nodes[found->second].buildable_target
                                              : properties.nodes[found->second].buildable_host;
                if (!consumer.value || dependency.value) continue;
                const auto name = lane ? "buildable-target" : "buildable-host";
                std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                          << nodes[i].name << " is " << name << ", but " << used
                          << " is not (" << name << " no, "
                          << (dependency.origin == mm::configure::OptionOrigin::Assignment
                                  ? "assigned by " : "reset by ")
                          << dependency.value_source.generic_string() << ")\n";
                ok = false;
            }

            const auto& consumer_core = properties.nodes[i].core;
            const auto& dependency_core = properties.nodes[found->second].core;
            if (consumer_core.value && !dependency_core.value) {
                std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                          << nodes[i].name << " is core, but " << used
                          << " is not (core no, "
                          << (dependency_core.origin == mm::configure::OptionOrigin::Assignment
                                  ? "assigned by " : "reset by ")
                          << dependency_core.value_source.generic_string() << ")\n";
                ok = false;
            }

            if (!nodes[i].external && nodes[found->second].external) {
                std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                          << nodes[i].name
                          << " is a project node, but uses external module "
                          << used << "\n";
                ok = false;
            }
        }
    }
    return ok;
}
std::vector<mm::configure::OptionNode> configuration_nodes(const Project& project) {
    std::vector<mm::configure::OptionNode> nodes;
    if (!project.ok || project.nodes.size() != project.documents.size()) return nodes;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        const auto& doc = project.documents[i];
        mm::configure::OptionNode opt_node{
            node.manifest, node.dir, node.name, node.kind,
            first(doc, "module"), all(doc, "use"), node.parent,
            all(doc, "option"), all(doc, "reset"), all(doc, "read-only"),
            first(doc, "library")};
        opt_node.external = node.external;
        opt_node.non_core = node.non_core;
        nodes.push_back(std::move(opt_node));
    }
    return nodes;
}

bool resolve_structural_properties(const std::filesystem::path& project_root, Build build,
                                   const Project& project, StructuralProperties& properties,
                                   std::string_view tool) {
    auto nodes = configuration_nodes(project);
    for (auto& node : nodes) {
        node.options = structural_property_declarations(node.options);
        node.resets = structural_property_declarations(node.resets);
        node.read_only = structural_property_declarations(node.read_only);
    }

    std::vector<mm::configure::OptionValues> resolved;
    if (!mm::configure::resolve_options(project_root, build, nodes, resolved, tool))
        return false;
    properties = structural_properties(resolved);
    return validate_structural_properties(nodes, properties, tool);
}

std::vector<bool> StructuralProperties::lane(bool target_lane,
                                             bool target_has_host_capability) const {
    std::vector<bool> result;
    result.reserve(nodes.size());
    if (!target_lane) {
        for (const auto& node : nodes) result.push_back(node.buildable_host.value);
        return result;
    }
    if (!target_has_host_capability) {
        for (const auto& node : nodes) result.push_back(node.buildable_target.value);
        return result;
    }

    for (const auto& node : nodes)
        result.push_back(node.buildable_target.value || node.buildable_host.value);
    return result;
}

bool validate_library_checkout(const std::filesystem::path& project_root,
                               const LibraryDefinition& library, std::string_view tool) {
    const auto source = absolute_from_root(project_root, library.source);
    bool present = false;
    if (!observe_checkout(source, present, library.manifest, tool)) return false;
    if (!present) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(source, ec);
        if (ec) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": cannot inspect library source " << source.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        const bool directory = exists && std::filesystem::is_directory(source, ec);
        if (ec) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": cannot inspect library source " << source.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (exists && !directory) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": library source is not a directory: " << source.string() << "\n";
            return false;
        }
        const auto provision = library.manifest.parent_path() / "vendor.sh";
        const auto provision_status = std::filesystem::symlink_status(provision, ec);
        const auto executable = std::filesystem::perms::owner_exec |
                                std::filesystem::perms::group_exec |
                                std::filesystem::perms::others_exec;
        const bool has_provision = !ec && std::filesystem::is_regular_file(provision_status) &&
                                   (provision_status.permissions() & executable) !=
                                       std::filesystem::perms::none;
        std::cerr << tool << ": library " << library.name << " checkout is absent: "
                  << library.source.generic_string();
        if (has_provision) std::cerr << "; run " << shell_quote(provision);
        std::cerr << "\n";
        return false;
    }

    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(project_root, ec);
    if (ec) return false;
    const auto canonical_source = std::filesystem::weakly_canonical(source, ec);
    if (ec || !path_within(root, canonical_source)) {
        std::cerr << tool << ": " << library.manifest.string()
                  << ": library source resolves outside the project: "
                  << library.source.generic_string() << "\n";
        return false;
    }
    for (const auto* paths : {&library.include_directories, &library.library_directories,
                              &library.link_archives}) {
        for (const auto& entry : *paths) {
            const auto candidate = absolute_from_root(project_root, entry.path);
            const bool exists = std::filesystem::exists(candidate, ec);
            if (ec) {
                std::cerr << tool << ": " << library.manifest.string()
                          << ": cannot inspect library interface path " << candidate.string()
                          << ": " << ec.message() << "\n";
                return false;
            }
            if (!exists) continue;
            const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
            if (ec || !path_within(canonical_source, canonical)) {
                std::cerr << tool << ": " << library.manifest.string()
                          << ": library interface path escapes source: "
                          << entry.path.generic_string() << "\n";
                return false;
            }
        }
    }
    const auto licence = std::filesystem::weakly_canonical(
        absolute_from_root(project_root, library.licence), ec);
    if (ec || path_within(canonical_source, licence)) {
        std::cerr << tool << ": " << library.manifest.string()
                  << ": licence must resolve outside library source\n";
        return false;
    }
    if (!library.external_build.empty()) {
        const auto cmake_dir = std::filesystem::weakly_canonical(
            absolute_from_root(project_root, library.manifest.parent_path() / "cmake"), ec);
        if (ec || path_within(canonical_source, cmake_dir)) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": cmake directory must resolve outside library source\n";
            return false;
        }
    }
    return true;
}

bool library_include_directories(
    const std::filesystem::path& project_root,
    const std::vector<LibraryDefinition>& libraries,
    const BuildableNode& target,
    std::vector<std::filesystem::path>& directories,
    std::string_view tool) {
    directories.clear();

    // A sketch application names its libraries by directory rather than by a
    // library: reference, so the two sources of include directories are
    // independent: an application has sketch libraries and no library:, a
    // wrapper module the reverse.
    if (!target.sketch_libraries.empty()) {
        // The application's own directory comes first: it holds the generated
        // header, which a library header includes before anything of the
        // library's own is found.
        directories.push_back(target.source_dir);
    }
    for (const auto& lib_root : target.sketch_libraries) {
        std::error_code sec;
        const auto src_dir = lib_root / "src";
        const bool layered =
            std::filesystem::is_directory(src_dir, sec) && !sec;
        directories.push_back(layered ? src_dir : lib_root);
    }

    if (target.library.empty()) return true;

    const LibraryDefinition* definition = nullptr;
    for (const auto& library : libraries)
        if (library.name == target.library) definition = &library;
    if (definition == nullptr) {
        std::cerr << tool << ": " << (target.dir / "mm.mdy").string()
                  << ": module references unknown library: " << target.library << "\n";
        return false;
    }

    std::error_code ec;
    const auto root = std::filesystem::absolute(project_root, ec).lexically_normal();
    if (ec) {
        std::cerr << tool << ": cannot resolve project root " << project_root.string()
                  << ": " << ec.message() << "\n";
        return false;
    }

    if (!validate_library_checkout(root, *definition, tool)) {
        // The checkout validator reports the resource failure and recovery
        // command; this second diagnostic deliberately names its consumer.
        std::cerr << tool << ": " << (target.dir / "mm.mdy").string()
                  << ": module " << target.name << " cannot use library "
                  << definition->name << "\n";
        return false;
    }

    for (const auto& include : definition->include_directories) {
        // Stage 2 constructs Source entries only. This guard becomes reachable
        // when the external-build stage introduces BuildPrefix interfaces.
        if (include.base != LibraryPathBase::Source) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": build-prefix include directory has no producer\n";
            return false;
        }
        const auto path = absolute_from_root(root, include.path);
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": cannot inspect library include directory " << path.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (!exists) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": library include directory does not exist: "
                      << include.path.generic_string() << "\n";
            return false;
        }
        const bool directory = std::filesystem::is_directory(path, ec);
        if (ec) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": cannot inspect library include directory " << path.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (!directory) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": library include path is not a directory: "
                      << include.path.generic_string() << "\n";
            return false;
        }
        directories.push_back(path);
    }
    return true;
}

const EffectiveProvider* PlatformProviders::binding(std::string_view interface_module) const {
    for (const auto& candidate : effective)
        if (candidate.interface_module == interface_module) return &candidate;
    return nullptr;
}

bool PlatformProviders::declares_provider(std::string_view module_name) const {
    return std::find(declared.begin(), declared.end(), module_name) != declared.end();
}

bool PlatformProviders::selects_provider(std::string_view module_name) const {
    for (const auto& candidate : effective)
        if (candidate.provider_module == module_name) return true;
    return false;
}

std::string PlatformProviders::unmet_requirement(std::size_t node) const {
    if (node >= requirements.size()) return {};
    for (const auto& interface_module : requirements[node]) {
        if (binding(interface_module) != nullptr) continue;
        std::string reason = "requires platform interface " + interface_module + ", but ";
        if (!selected_board.empty() && !selected_sdk.empty())
            reason += "board " + selected_board + " and SDK " + selected_sdk +
                      " provide no binding";
        else if (!selected_sdk.empty())
            reason += "SDK " + selected_sdk + " provides no binding";
        else
            reason += "no platform is selected";
        return reason;
    }
    return {};
}

PlatformProviders platform_providers(const Project& project, bool target_lane,
                                     const Platform* platform, std::string_view) {
    PlatformProviders providers;
    providers.requirements.resize(project.nodes.size());

    const auto modules = modules_by_module_name(project);
    for (const auto& target : project.targets)
        if (target.kind == "module" && target.platform_interface)
            providers.interfaces.push_back(target.module_name);

    // Declared, not selected: a provider is never an independent root, so this
    // set is what keeps an unbound implementation out of a root build even in a
    // lane that binds nothing.
    const auto declare = [&](const std::vector<PlatformProviderBinding>& declarations) {
        for (const auto& binding : declarations)
            if (!providers.declares_provider(binding.provider_module))
                providers.declared.push_back(binding.provider_module);
    };
    for (const auto& sdk : project.sdks) declare(sdk.providers);
    for (const auto& board : project.boards) declare(board.providers);

    // Selection. The SDK's defaults are laid down first and the selected
    // board's bindings overwrite them, which is the whole of board-over-SDK
    // precedence: structural, and independent of declaration or folder order.
    const auto bind = [&](const std::string& owner,
                          const std::vector<PlatformProviderBinding>& declarations,
                          bool from_board) {
        for (const auto& binding : declarations) {
            EffectiveProvider resolved{binding.interface_module, binding.provider_module, owner,
                                       from_board};
            bool replaced = false;
            for (auto& existing : providers.effective) {
                if (existing.interface_module != binding.interface_module) continue;
                existing = resolved;
                replaced = true;
                break;
            }
            if (!replaced) providers.effective.push_back(std::move(resolved));
        }
    };

    if (target_lane && platform != nullptr) {
        if (platform->sdk) {
            providers.selected_sdk = *platform->sdk;
            for (const auto& sdk : project.sdks)
                if (sdk.name == providers.selected_sdk) bind(sdk.name, sdk.providers, false);
        }
        if (platform->board) {
            providers.selected_board = *platform->board;
            for (const auto& board : project.boards)
                if (board.name == providers.selected_board)
                    bind(board.name, board.providers, true);
        }
    }

    // Requirements are computed over the complete, unfiltered target
    // collection. Asking an already filtered tree for them would mean asking it
    // to discover the dependency that decides whether a node survives
    // filtering.
    const auto collect = [&](const BuildableNode& root, std::vector<std::string>& out) {
        std::vector<std::size_t> pending;
        std::set<std::size_t> seen;
        for (const auto& used : root.uses) {
            const auto entry = modules.find(used);
            if (entry != modules.end()) pending.push_back(entry->second);
        }
        while (!pending.empty()) {
            const auto index = pending.back();
            pending.pop_back();
            if (!seen.insert(index).second) continue;
            const auto& target = project.targets[index];
            if (target.platform_interface &&
                std::find(out.begin(), out.end(), target.module_name) == out.end())
                out.push_back(target.module_name);
            for (const auto& used : target.uses) {
                const auto entry = modules.find(used);
                if (entry != modules.end()) pending.push_back(entry->second);
            }
        }
    };

    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (project.target[i] == no_target) continue;
        const auto& kind = project.nodes[i].kind;
        if (kind == "test")
            collect(project.tests[project.target[i]], providers.requirements[i]);
        else if (kind == "app" || kind == "module")
            collect(project.targets[project.target[i]], providers.requirements[i]);
    }

    // A selected provider may itself use another platform interface. Expand
    // each requirement set to a fixed point so availability catches a missing
    // nested binding before compilation, and so this answer is independent of
    // manifest walk order. The provider's required edge back to the interface
    // it implements is already present and therefore terminates naturally.
    for (auto& required : providers.requirements) {
        for (std::size_t next = 0; next < required.size(); ++next) {
            const std::string interface_module = required[next];
            const auto* selected = providers.binding(interface_module);
            if (selected == nullptr) continue;
            const auto provider = modules.find(selected->provider_module);
            if (provider == modules.end()) continue;  // rejected during load
            collect(project.targets[provider->second], required);
        }
    }

    return providers;
}

Availability availability(const Project& project, std::size_t node, bool capability,
                          bool target_lane, const Platform* platform,
                          const PlatformProviders* providers,
                          const std::vector<bool>* lane_capabilities) {
    if (node >= project.nodes.size()) return {false, "manifest node is not registered"};
    if (!capability) {
        return {false, project.nodes[node].name + " is not buildable-" +
                           (target_lane ? "target" : "host")};
    }
    if (providers != nullptr && project.nodes[node].kind == "module" &&
        project.target[node] != no_target) {
        const auto& target = project.targets[project.target[node]];
        if (providers->declares_provider(target.module_name) &&
            !providers->selects_provider(target.module_name)) {
            return {false, target.name + " implements platform interface " +
                               target.module_name + ", which the selected platform does not bind"};
        }
    }
    if (project.nodes[node].kind == "module" && project.target[node] != no_target) {
        const auto& target = project.targets[project.target[node]];
        if (!target.library.empty()) {
            const LibraryDefinition* library = nullptr;
            for (const auto& candidate : project.libraries) {
                if (candidate.name == target.library) {
                    library = &candidate;
                    break;
                }
            }
            if (library != nullptr && !library->external_build.empty()) {
                const SdkDefinition* selected_sdk = nullptr;
                if (target_lane && platform != nullptr && platform->sdk) {
                    for (const auto& candidate : project.sdks) {
                        if (candidate.name == *platform->sdk) {
                            selected_sdk = &candidate;
                            break;
                        }
                    }
                }
                if (selected_sdk == nullptr || selected_sdk->library != library->name) {
                    return {false, project.nodes[node].name + " requires an SDK naming library " +
                                       library->name};
                }
            }
        }
    }
    // The host lane selects no platform at all, and the interface's own
    // implementation unit answers Unsupported there, so a requirement is only
    // unmet where a platform is modelled and still binds nothing.
    if (!target_lane || project.target[node] == no_target) return {true, {}};

    const BuildableNode* buildable = nullptr;
    if (project.nodes[node].kind == "test")
        buildable = &project.tests[project.target[node]];
    else if (project.nodes[node].kind == "app" || project.nodes[node].kind == "module")
        buildable = &project.targets[project.target[node]];
    if (buildable == nullptr) return {true, {}};

    if (providers != nullptr &&
        (project.nodes[node].kind == "app" || project.nodes[node].kind == "test")) {
        const auto unmet = providers->unmet_requirement(node);
        if (!unmet.empty()) return {false, buildable->name + " " + unmet};

        // A binding is not usable merely because its name resolves. Structural
        // capability filtering happens before the Tree is built; without this
        // check a selected provider carrying buildable-target no would vanish
        // from that tree while the executable remained available and linked
        // the interface fallback silently.
        if (lane_capabilities != nullptr) {
            for (const auto& interface_module : providers->requirements[node]) {
                const auto* binding = providers->binding(interface_module);
                if (binding == nullptr) continue;  // reported by unmet_requirement above
                for (std::size_t provider_node = 0;
                     provider_node < project.nodes.size(); ++provider_node) {
                    if (project.nodes[provider_node].kind != "module" ||
                        project.target[provider_node] == no_target)
                        continue;
                    const auto& provider = project.targets[project.target[provider_node]];
                    if (provider.module_name != binding->provider_module) continue;
                    if (provider_node >= lane_capabilities->size() ||
                        !(*lane_capabilities)[provider_node]) {
                        return {false, buildable->name + " requires platform provider " +
                                           binding->provider_module + " for interface " +
                                           interface_module + ", but " + provider.name +
                                           " is not buildable-" +
                                           (target_lane ? "target" : "host")};
                    }
                    break;
                }
            }
        }
    }

    if (buildable->requires_board.empty()) return {true, {}};

    const std::string selected = platform != nullptr && platform->board
                                     ? *platform->board
                                     : std::string("none");
    if (selected == buildable->requires_board) return {true, {}};
    return {false, buildable->name + " requires board " + buildable->requires_board +
                       "; selected board is " + selected};
}

bool can_link_executable(const Platform* platform, std::string_view tool,
                         std::string_view name) {
    if (platform == nullptr || !platform->models_responsibilities || platform->unresolved.empty())
        return true;
    std::cerr << tool << ": " << name << ": unresolved platform responsibility: "
              << mm::configure::responsibility_name(platform->unresolved.front()) << "\n";
    return false;
}

bool check_configuration_staleness(const BuildConfiguration& configuration,
                                   const Project& project,
                                   bool target_lane,
                                   std::string_view tool) {
    if (!target_lane) return true;
    const auto* platform = configuration.configured_target_platform();
    if (platform == nullptr || !platform->sdk) return true;

    const SdkDefinition* sdk = nullptr;
    for (const auto& entry : project.sdks) {
        if (entry.name == *platform->sdk) {
            sdk = &entry;
            break;
        }
    }
    if (sdk == nullptr) {
        std::cerr << tool << ": stale configuration record: selected SDK \"" << *platform->sdk
                  << "\" is not in the project; rerun configure\n";
        return false;
    }

    const LibraryDefinition* library = nullptr;
    if (!sdk->library.empty()) {
        for (const auto& lib : project.libraries) {
            if (lib.name == sdk->library) {
                library = &lib;
                break;
            }
        }
        if (library == nullptr) {
            std::cerr << tool << ": stale configuration record: library \"" << sdk->library
                      << "\" is not in the project; rerun configure\n";
            return false;
        }
    }

    const auto expected = (library != nullptr && !library->external_build.empty())
                              ? mm::configure::LinkOwnership::External
                              : mm::configure::LinkOwnership::Project;

    if (platform->link_ownership != expected) {
        if (library != nullptr) {
            std::cerr << tool << ": stale configuration record: library \""
                      << library->name
                      << "\" external-build declaration does not match cross-link; rerun configure\n";
        } else {
            std::cerr << tool << ": stale configuration record: SDK \""
                      << sdk->name
                      << "\" names no external library; rerun configure\n";
        }
        return false;
    }

    if (platform->board) {
        const BoardDefinition* board = nullptr;
        for (const auto& entry : project.boards) {
            if (entry.name == *platform->board) {
                board = &entry;
                break;
            }
        }
        if (board == nullptr) {
            std::cerr << tool << ": stale configuration record: selected board \""
                      << *platform->board << "\" is not in the project; rerun configure\n";
            return false;
        }
        std::vector<std::string> recorded_chain;
        recorded_chain.push_back(*platform->board);
        for (const auto& base : platform->board_derives_from) {
            recorded_chain.push_back(base);
        }
        if (board->chain != recorded_chain) {
            std::cerr << tool << ": stale configuration record: board \"" << *platform->board
                      << "\" derivation chain changed (recorded:";
            for (const auto& name : recorded_chain) std::cerr << " " << name;
            std::cerr << ", live:";
            for (const auto& name : board->chain) std::cerr << " " << name;
            std::cerr << "); rerun configure\n";
            return false;
        }
    }
    return true;
}

std::optional<BuildableNode> platform_unit(const Platform* platform) {
    if (platform == nullptr || !platform->board || platform->board_sources.empty())
        return std::nullopt;
    BuildableNode unit;
    unit.kind = "board";
    unit.name = *platform->board;
    if (platform->board_manifest) unit.dir = platform->board_manifest->parent_path();
    for (const auto& source : platform->board_sources)
        unit.sources.push_back({source.generic_string(), {}});
    return unit;
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

    if (!valid_manifest(doc, first(doc, "kind"), first(doc, "name"), manifest_path, policy))
        return target;

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
    target.requires_board = first(doc, "requires-board");

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
}
