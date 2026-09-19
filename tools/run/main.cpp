// Execute one previously built application in the selected lane.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

import mm.app;
import mm.build;
import mm.configure;
import mm.run;

int main(int argc, char** argv) {
    mm::app::Options options("run");
    options.flag("--host");
    options.flag("--target");
    options.separator();
    options.help("run [-v|--verbose] [-h|--help] [--host | --target] "
                 "<app-manifest> [-- arguments...]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;
    if (options.count("--host") > 1 || options.count("--target") > 1) {
        std::cerr << "run: lane option may be given only once\n";
        return mm::build::exit_usage;
    }
    if (options.seen("--host") && options.seen("--target")) {
        std::cerr << "run: --host and --target are mutually exclusive\n";
        return mm::build::exit_usage;
    }
    if (options.positional().empty()) {
        std::cerr << "usage: run [-v] [--host | --target] <app-manifest> "
                     "[-- arguments...]\n";
        return mm::build::exit_usage;
    }

    auto manifest = mm::build::resolve_manifest(options.positional().front());
    std::filesystem::path manifest_directory;
    if (const auto status = mm::app::open_manifest("run", manifest, manifest_directory, false);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage
                                              : mm::build::exit_manifest;

    const auto resolved_roots = mm::build::resolve_roots(manifest);
    if (!resolved_roots.ok) {
        std::cerr << "run: cannot resolve root for " << manifest.string()
                  << "\n";
        return mm::build::exit_manifest;
    }
    std::error_code ec;
    std::filesystem::current_path(resolved_roots.project_root, ec);
    if (ec) {
        std::cerr << "run: cannot enter project root: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::BuildConfiguration configuration;
    if (!mm::build::resolve_configuration(".", options.verbose(), configuration))
        return mm::build::exit_manifest;
    const bool target_lane = options.seen("--target") ||
        (!options.seen("--host") && configuration.selects_cross());
    const auto* toolchain = configuration.toolchain_for(target_lane);
    const auto* lane_directory = configuration.build_directory_for(target_lane);
    if (toolchain == nullptr || lane_directory == nullptr) {
        std::cerr << "run: target lane is not configured\n";
        return mm::build::exit_manifest;
    }

    mm::build::LoadPolicy policy{.tool = "run", .warn_options = true};
    if (resolved_roots.external_root)
        policy.external = resolved_roots.external_root;
    auto project = mm::build::load_project(".", policy);
    if (!project.ok) return mm::build::exit_manifest;
    if (!mm::build::check_configuration_staleness(configuration, project, target_lane, "run"))
        return mm::build::exit_manifest;
    std::size_t node = mm::build::no_target;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (project.nodes[i].source_dir == resolved_roots.requested_dir) {
            node = i;
            break;
        }
    }
    if (node == mm::build::no_target || project.nodes[node].kind != "app") {
        std::cerr << "run: requested manifest is not a registered app: "
                  << manifest.string() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::StructuralProperties properties;
    if (!mm::build::resolve_structural_properties(".", configuration.build, project, properties,
                                                  "run"))
        return mm::build::exit_manifest;
    const auto buildable = properties.lane(
        target_lane, configuration.target_has_host_capability());
    const auto available = mm::build::availability(
        project, node, buildable[node], target_lane,
        target_lane ? configuration.configured_target_platform() : &configuration.host_platform());
    if (!available.available) {
        std::cerr << "run: " << project.nodes[node].manifest.string() << ": "
                  << available.reason << "\n";
        return mm::build::exit_unavailable;
    }

    const auto& app = project.targets[project.target[node]];
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
    const auto executable = context.executable_path(app);
    if (!std::filesystem::is_regular_file(executable, ec) || ec) {
        std::cerr << "run: application is not built: " << executable.string()
                  << "; run build first\n";
        return mm::build::exit_run;
    }
    if (target_lane && !toolchain->runner) {
        std::cerr << "run: target " << toolchain->target << " has no runner\n";
        return mm::build::exit_run;
    }

    if (options.verbose()) {
        std::cout << "modules.cpp run tool\n";
        std::cout << "  app    " << app.name << "\n";
        std::cout << "  target " << executable.string() << "\n";
    }

    std::filesystem::current_path(
        app.external ? app.source_dir : resolved_roots.project_root, ec);
    if (ec) {
        std::cerr << "run: cannot enter "
                  << (app.external ? app.source_dir
                                   : resolved_roots.project_root).string()
                  << ": " << ec.message() << "\n";
        return mm::build::exit_run;
    }

    const int status = mm::run::execute(*toolchain, target_lane, executable,
                                        options.trailing());
    return status < 0 ? mm::build::exit_run : status;
}
