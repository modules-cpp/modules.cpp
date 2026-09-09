// Debug one previously built application in the selected lane.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <iostream>
#include <string>

import mm.app;
import mm.build;
import mm.debug;

int main(int argc, char** argv) {
    mm::app::Options options("debug");
    options.flag("--host");
    options.flag("--target");
    options.separator();
    options.help("debug [-v|--verbose] [-h|--help] [--host | --target] "
                 "<app-manifest> [-- arguments...]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;
    if (options.count("--host") > 1 || options.count("--target") > 1) {
        std::cerr << "debug: lane option may be given only once\n";
        return mm::build::exit_usage;
    }
    if (options.seen("--host") && options.seen("--target")) {
        std::cerr << "debug: --host and --target are mutually exclusive\n";
        return mm::build::exit_usage;
    }
    if (options.positional().empty()) {
        std::cerr << "usage: debug [-v] [--host | --target] <app-manifest> "
                     "[-- arguments...]\n";
        return mm::build::exit_usage;
    }

    auto manifest = mm::build::resolve_manifest(options.positional().front());
    std::filesystem::path manifest_directory;
    if (const auto status = mm::app::open_manifest("debug", manifest, manifest_directory, false);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage
                                              : mm::build::exit_manifest;

    const auto root = mm::build::find_project_root(manifest_directory);
    if (root.empty()) {
        std::cerr << "debug: no kind:project mm.mdy above " << manifest_directory.string()
                  << "\n";
        return mm::build::exit_manifest;
    }
    std::error_code ec;
    const auto requested = std::filesystem::weakly_canonical(manifest, ec);
    if (ec) {
        std::cerr << "debug: cannot resolve manifest: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }
    std::filesystem::current_path(root, ec);
    if (ec) {
        std::cerr << "debug: cannot enter project root: " << ec.message() << "\n";
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
        std::cerr << "debug: target lane is not configured\n";
        return mm::build::exit_manifest;
    }

    auto project = mm::build::load_project(".", {.tool = "debug", .warn_options = true});
    if (!project.ok) return mm::build::exit_manifest;
    std::size_t node = mm::build::no_target;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto candidate = std::filesystem::weakly_canonical(project.nodes[i].manifest, ec);
        if (!ec && candidate == requested) {
            node = i;
            break;
        }
        ec.clear();
    }
    if (node == mm::build::no_target || project.nodes[node].kind != "app") {
        std::cerr << "debug: requested manifest is not a registered app: "
                  << manifest.string() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::BuildCapabilities capabilities;
    if (!mm::build::resolve_capabilities(".", configuration.build, project, capabilities,
                                         "debug"))
        return mm::build::exit_manifest;
    const auto buildable = capabilities.lane(
        target_lane, configuration.target_has_host_capability());
    const auto available = mm::build::availability(
        project, node, buildable[node], target_lane,
        target_lane ? configuration.configured_target_platform() : &configuration.host_platform());
    if (!available.available) {
        std::cerr << "debug: " << project.nodes[node].manifest.string() << ": "
                  << available.reason << "\n";
        return mm::build::exit_unavailable;
    }

    const auto& app = project.targets[project.target[node]];
    const auto executable = *lane_directory / (app.dir / app.name).lexically_normal();
    if (!std::filesystem::is_regular_file(executable, ec) || ec) {
        std::cerr << "debug: application is not built: " << executable.string()
                  << "; run build first\n";
        return mm::build::exit_run;
    }
    if (!toolchain->debugger) {
        std::cerr << "debug: " << (target_lane ? "target " + toolchain->target : "host")
                  << " has no debugger; rerun configure with --debugger gdb\n";
        return mm::build::exit_run;
    }
    if (target_lane && !toolchain->runner) {
        std::cerr << "debug: target " << toolchain->target << " has no runner\n";
        return mm::build::exit_run;
    }

    if (options.verbose()) {
        std::cout << "modules.cpp debug tool\n";
        std::cout << "  app      " << app.name << "\n";
        std::cout << "  debugger " << toolchain->debugger->invocation << "\n";
        std::cout << "  target   " << executable.string() << "\n";
    }
    const int status = mm::debug::execute(*toolchain, target_lane, executable,
                                          options.trailing());
    return status < 0 ? mm::build::exit_run : status;
}
