// Shared tool front end implementation.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <system_error>
#include <filesystem>
#include <optional>
#include <iostream>
#include <string>
#include <string_view>

module mm.tool;

namespace mm::tool {

int cli_status(mm::app::Cli status) {
    switch (status) {
        case mm::app::Cli::ok:
        case mm::app::Cli::help:
            return mm::build::exit_ok;
        case mm::app::Cli::usage:
            return mm::build::exit_usage;
        default:
            return mm::build::exit_manifest;
    }
}

int manifest_status(mm::app::Cli status) {
    if (status == mm::app::Cli::ok) return mm::build::exit_ok;
    if (status == mm::app::Cli::usage) return mm::build::exit_usage;
    return mm::build::exit_manifest;
}

void banner(std::ostream& out, std::string_view name, const std::filesystem::path& root) {
    out << "modules.cpp " << name << " tool\n";
    out << "  root " << root.string() << "\n\n";
}

int run_status(int status) { return status < 0 ? mm::build::exit_run : status; }

std::optional<std::string> lane_error(int host_count, int target_count) {
    if (host_count > 1 || target_count > 1) return "lane option may be given only once";
    if (host_count > 0 && target_count > 0) return "--host and --target are mutually exclusive";
    return std::nullopt;
}

bool select_lane(bool host_flag, bool target_flag, bool configuration_selects_cross) {
    return target_flag || (!host_flag && configuration_selects_cross);
}

int resolve_app_target(const Identity& identity,
                       const std::filesystem::path& manifest,
                       int host_count,
                       int target_count,
                       std::string_view node_kind,
                       ResolvedTarget& out) {
    const std::string_view name = identity.name;

    if (const auto error = lane_error(host_count, target_count)) {
        std::cerr << name << ": " << *error << "\n";
        return mm::build::exit_usage;
    }

    std::filesystem::path manifest_directory;
    if (const auto status = mm::app::open_manifest(name, manifest, manifest_directory, false);
        status != mm::app::Cli::ok)
        return manifest_status(status);

    const auto resolved_roots = mm::build::resolve_roots(manifest);
    if (!resolved_roots.ok) {
        std::cerr << name << ": cannot resolve root for " << manifest.string() << "\n";
        return mm::build::exit_manifest;
    }
    std::error_code ec;
    std::filesystem::current_path(resolved_roots.project_root, ec);
    if (ec) {
        std::cerr << name << ": cannot enter project root: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::BuildConfiguration configuration;
    if (!mm::build::resolve_configuration(".", identity.verbose, configuration))
        return mm::build::exit_manifest;
    const bool target_lane =
        select_lane(host_count > 0, target_count > 0, configuration.selects_cross());
    const auto* toolchain = configuration.toolchain_for(target_lane);
    const auto* lane_directory = configuration.build_directory_for(target_lane);
    if (toolchain == nullptr || lane_directory == nullptr) {
        std::cerr << name << ": target lane is not configured\n";
        return mm::build::exit_manifest;
    }

    mm::build::LoadPolicy policy{.tool = name, .warn_options = true};
    if (resolved_roots.external_root) policy.external = resolved_roots.external_root;
    auto project = mm::build::load_project(".", policy);
    if (!project.ok) return mm::build::exit_manifest;
    if (!mm::build::check_configuration_staleness(configuration, project, target_lane, name))
        return mm::build::exit_manifest;

    std::size_t node = mm::build::no_target;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (project.nodes[i].source_dir == resolved_roots.requested_dir) {
            node = i;
            break;
        }
    }
    if (node == mm::build::no_target || project.nodes[node].kind != node_kind ||
        project.target[node] == mm::build::no_target) {
        std::cerr << name << ": requested manifest is not a registered " << node_kind
                  << ": " << manifest.string() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::StructuralProperties properties;
    if (!mm::build::resolve_structural_properties(".", configuration.build, project, properties,
                                                  name))
        return mm::build::exit_manifest;
    const auto buildable =
        properties.lane(target_lane, configuration.target_has_host_capability());
    const auto available = mm::build::availability(
        project, node, buildable[node], target_lane,
        target_lane ? configuration.configured_target_platform()
                    : &configuration.host_platform());
    if (!available.available) {
        std::cerr << name << ": " << project.nodes[node].manifest.string() << ": "
                  << available.reason << "\n";
        return mm::build::exit_unavailable;
    }

    const mm::build::BuildableNode& target_node = node_kind == "test"
                                                       ? project.tests[project.target[node]]
                                                       : project.targets[project.target[node]];
    const auto context_tree_root = resolved_roots.external_root
                                       ? *resolved_roots.external_root
                                       : resolved_roots.project_root;
    const auto context_output_root =
        resolved_roots.external_root
            ? (*resolved_roots.external_root / *lane_directory)
            : (resolved_roots.project_root / *lane_directory);
    mm::build::ArtifactContext context(
        context_tree_root, context_output_root, resolved_roots.tools_dir,
        resolved_roots.external_root.has_value());
    const auto executable = context.executable_path(target_node);
    if (!std::filesystem::is_regular_file(executable, ec) || ec) {
        std::cerr << name << ": application is not built: " << executable.string()
                  << "; run build first\n";
        return mm::build::exit_run;
    }

    out.configuration = configuration;
    out.target_lane = target_lane;
    out.project = project;
    out.node = node;
    out.executable_path = executable;
    out.context = std::move(context);
    return mm::build::exit_ok;
}

}  // namespace mm::tool
