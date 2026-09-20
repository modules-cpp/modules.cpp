// Flash one previously built target application.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

import mm.app;
import mm.build;
import mm.flash;
import mm.tool;

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

    const auto state = mm::tool::setup({
        .tool = "flash",
        .manifest = manifest,
        .force_target_lane = true,
        .verbose = options.verbose(),
        .node_kind = "app",
        .resolve_providers = true,
        .check_providers_ok = true,
        .lane_not_configured = "no target lane is configured",
    });
    if (!state.ok) return state.status;

    // Domain policy for this tool: only Pico SDK platforms flash, and only
    // through the configured board.
    const mm::build::BoardDefinition* selected_board = nullptr;
    for (const auto& board : state.project.boards)
        if (state.platform && state.platform->board && board.name == *state.platform->board)
            selected_board = &board;
    if (selected_board == nullptr || !mm::flash::supports(*state.platform, *selected_board)) {
        std::cerr << "flash: configured target is not a supported Pico SDK platform\n";
        return mm::build::exit_unavailable;
    }

    const auto& app = state.project.targets[state.project.target[state.node]];
    const auto context = state.artifact_context();
    const auto executable = context.executable_path(app);
    const auto image = mm::flash::image_for(executable);
    std::error_code ec;
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
        std::cout << "  board    " << *state.platform->board << "\n";
        std::cout << "  image    " << image.string() << "\n";
        std::cout << "  picotool " << picotool.string() << "\n";
    }
    const int status = mm::flash::execute(state.toolchain, picotool, image);
    return status < 0 ? mm::build::exit_run : status;
}
