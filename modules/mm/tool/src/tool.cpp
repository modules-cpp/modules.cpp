// The shared front-end prologue implementation.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module mm.tool;

import mm.app;
import mm.build;

namespace mm::tool {
namespace {

// A setup step that did not complete. The reason is printed under the tool
// name unless the shared step already printed its own diagnostic.
[[nodiscard]] Setup failed(int status, const Options& options,
                           std::string message = {}, bool silent = false) {
    Setup setup;
    setup.ok = false;
    setup.status = status;
    if (!silent)
        std::cerr << options.tool << ": " << message << "\n";
    return setup;
}

}  // namespace

Setup setup(const Options& options) {
    // The requested manifest canonicalizes against the caller's working
    // directory, before this function enters the project root.
    std::error_code ec;
    std::filesystem::path requested;
    if (options.match_by_manifest_path) {
        requested = std::filesystem::weakly_canonical(options.manifest, ec);
        if (ec)
            return failed(mm::build::exit_manifest, options,
                          "cannot resolve manifest: " + ec.message());
    }

    Setup setup;
    setup.roots = mm::build::resolve_roots(options.manifest);
    if (!setup.roots.ok)
        return failed(mm::build::exit_manifest, options,
                      "cannot resolve root for " + options.manifest.string());
    if (options.reject_external_root && setup.roots.external_root)
        return failed(mm::build::exit_manifest, options,
                      options.manifest.string() + ": " +
                      std::string(options.external_root_rejection));

    std::filesystem::current_path(setup.roots.project_root, ec);
    if (ec)
        return failed(mm::build::exit_manifest, options,
                      "cannot enter project root: " + ec.message());

    setup.configuration = mm::build::BuildConfiguration{};
    if (!mm::build::resolve_configuration(".", options.verbose, setup.configuration))
        return failed(mm::build::exit_manifest, options, {}, true);

    setup.target_lane = options.force_target_lane ||
                        (options.target ||
                         (!options.host && setup.configuration.selects_cross()));
    const auto* toolchain = setup.configuration.toolchain_for(setup.target_lane);
    const auto* lane_directory = setup.configuration.build_directory_for(setup.target_lane);
    const auto* platform = setup.target_lane
                               ? setup.configuration.configured_target_platform()
                               : &setup.configuration.host_platform();
    if (toolchain == nullptr || lane_directory == nullptr ||
        (options.force_target_lane && platform == nullptr))
        return failed(mm::build::exit_manifest, options,
                      std::string(options.lane_not_configured));

    setup.toolchain = *toolchain;
    setup.lane_directory = *lane_directory;
    if (platform != nullptr)
        setup.platform = *platform;

    mm::build::LoadPolicy policy{.tool = options.tool, .warn_options = true};
    if (!options.reject_external_root && setup.roots.external_root)
        policy.external = setup.roots.external_root;
    setup.project = mm::build::load_project(".", policy);
    if (!setup.project.ok)
        return failed(mm::build::exit_manifest, options, {}, true);
    if (!mm::build::check_configuration_staleness(
            setup.configuration, setup.project, setup.target_lane, options.tool))
        return failed(mm::build::exit_manifest, options, {}, true);

    setup.node = mm::build::no_target;
    for (std::size_t i = 0; i < setup.project.nodes.size(); ++i) {
        if (options.match_by_manifest_path) {
            const auto candidate =
                std::filesystem::weakly_canonical(setup.project.nodes[i].manifest, ec);
            if (ec)
                return failed(mm::build::exit_manifest, options,
                              "cannot resolve manifest: " + ec.message());
            if (candidate == requested) {
                setup.node = i;
                break;
            }
        } else if (setup.project.nodes[i].source_dir == setup.roots.requested_dir) {
            setup.node = i;
            break;
        }
    }
    if (setup.node == mm::build::no_target)
        return failed(mm::build::exit_manifest, options,
                      "requested manifest is not in the project tree: " +
                      options.manifest.string());
    if (!options.node_kind.empty() &&
        setup.project.nodes[setup.node].kind != options.node_kind)
        return failed(mm::build::exit_manifest, options,
                      "requested manifest is not a registered " +
                      std::string(options.node_kind) + ": " + options.manifest.string());

    mm::build::StructuralProperties properties;
    if (!mm::build::resolve_structural_properties(".", setup.configuration.build,
                                                   setup.project, properties,
                                                   options.tool))
        return failed(mm::build::exit_manifest, options, {}, true);
    setup.buildable = properties.lane(
        setup.target_lane, setup.configuration.target_has_host_capability());

    if (options.resolve_providers) {
        const auto providers = mm::build::platform_providers(
            setup.project, setup.target_lane, setup.platform ? &*setup.platform : nullptr,
            options.tool);
        if (options.check_providers_ok && !providers.ok)
            return failed(mm::build::exit_manifest, options, {}, true);
        setup.providers = std::move(providers);
    }

    const auto available = mm::build::availability(
        setup.project, setup.node, setup.buildable[setup.node], setup.target_lane,
        setup.platform ? &*setup.platform : nullptr,
        setup.providers ? &*setup.providers : nullptr, &setup.buildable);
    if (!available.available)
        return failed(mm::build::exit_unavailable, options,
                      setup.project.nodes[setup.node].manifest.string() + ": " +
                      available.reason);

    setup.context_tree_root = setup.roots.external_root
                                  ? *setup.roots.external_root
                                  : setup.roots.project_root;
    setup.context_output_root = setup.roots.external_root
                                    ? (*setup.roots.external_root / setup.lane_directory)
                                    : (setup.roots.project_root / setup.lane_directory);
    return setup;
}

}  // namespace mm::tool
