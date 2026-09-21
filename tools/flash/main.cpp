// Flash one previously built target application.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdlib>
#include <filesystem>
#include <iostream>

import mm.app;
import mm.build;
import mm.flash;

int main(int argc, char** argv) {
    mm::app::Options options("flash");
    options.help("flash [-v|--verbose] [-h|--help] <app-manifest>");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;
    if (options.positional().size() != 1) {
        std::cerr << "usage: flash [-v] <app-manifest>\n";
        return mm::build::exit_usage;
    }

    auto manifest = mm::build::resolve_manifest(options.positional().front());
    std::filesystem::path manifest_directory;
    if (const auto status = mm::app::open_manifest("flash", manifest, manifest_directory, false);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage
                                              : mm::build::exit_manifest;

    const auto resolved_roots = mm::build::resolve_roots(manifest);
    if (!resolved_roots.ok) {
        std::cerr << "flash: cannot resolve root for " << manifest.string()
                  << "\n";
        return mm::build::exit_manifest;
    }
    std::error_code ec;
    std::filesystem::current_path(resolved_roots.project_root, ec);
    if (ec) {
        std::cerr << "flash: cannot enter project root: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::BuildConfiguration configuration;
    if (!mm::build::resolve_configuration(".", options.verbose(), configuration))
        return mm::build::exit_manifest;
    const auto* toolchain = configuration.cross_toolchain();
    const auto* lane_directory = configuration.cross_build_directory();
    const auto* platform = configuration.configured_target_platform();
    if (toolchain == nullptr || lane_directory == nullptr || platform == nullptr) {
        std::cerr << "flash: no target lane is configured\n";
        return mm::build::exit_manifest;
    }
    mm::build::LoadPolicy policy{.tool = "flash", .warn_options = true};
    if (resolved_roots.external_root)
        policy.external = resolved_roots.external_root;
    auto project = mm::build::load_project(".", policy);
    if (!project.ok) return mm::build::exit_manifest;
    if (!mm::build::check_configuration_staleness(configuration, project, true, "flash"))
        return mm::build::exit_manifest;

    const mm::build::BoardDefinition* selected_board = nullptr;
    for (const auto& board : project.boards)
        if (platform->board && board.name == *platform->board) selected_board = &board;
    if (selected_board == nullptr || !mm::flash::supports(*platform, *selected_board)) {
        std::cerr << "flash: configured target is not a supported Pico SDK platform\n";
        return mm::build::exit_unavailable;
    }

    std::size_t node = mm::build::no_target;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (project.nodes[i].source_dir == resolved_roots.requested_dir) {
            node = i;
            break;
        }
    }
    if (node == mm::build::no_target || project.nodes[node].kind != "app") {
        std::cerr << "flash: requested manifest is not a registered app: "
                  << manifest.string() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::StructuralProperties properties;
    if (!mm::build::resolve_structural_properties(".", configuration.build, project, properties,
                                                  "flash"))
        return mm::build::exit_manifest;
    const auto buildable = properties.lane(true, configuration.target_has_host_capability());
    const auto providers = mm::build::platform_providers(project, true, platform, "flash");
    if (!providers.ok) return mm::build::exit_manifest;
    const auto available = mm::build::availability(project, node, buildable[node], true, platform,
                                                   &providers, &buildable);
    if (!available.available) {
        std::cerr << "flash: " << project.nodes[node].manifest.string() << ": "
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
    const auto image = mm::flash::image_for(executable);
    if (!std::filesystem::is_regular_file(image, ec) || ec) {
        std::cerr << "flash: UF2 image is not built: " << image.string()
                  << "; run build first\n";
        return mm::build::exit_run;
    }

    const char* package = std::getenv("picotool_DIR");
    if (package == nullptr || *package == '\0') {
        std::cerr << "flash: picotool_DIR is unset\n";
        return mm::build::exit_run;
    }
    auto package_directory = std::filesystem::weakly_canonical(package, ec);
    if (ec || !std::filesystem::is_directory(package_directory, ec) || ec) {
        std::cerr << "flash: picotool_DIR is not an existing directory: " << package << "\n";
        return mm::build::exit_run;
    }
    const auto picotool = package_directory / "picotool";
    if (!std::filesystem::is_regular_file(picotool, ec) || ec) {
        std::cerr << "flash: picotool executable not found: " << picotool.string() << "\n";
        return mm::build::exit_run;
    }

    if (options.verbose()) {
        std::cout << "modules.cpp flash tool\n";
        std::cout << "  board    " << *platform->board << "\n";
        std::cout << "  image    " << image.string() << "\n";
        std::cout << "  picotool " << picotool.string() << "\n";
    }
    const int status = mm::flash::execute(*toolchain, picotool, image);
    return status < 0 ? mm::build::exit_run : status;
}
