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
import mm.tool;

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

    const auto state = mm::tool::setup({
        .tool = "debug",
        .manifest = manifest,
        .host = options.seen("--host"),
        .target = options.seen("--target"),
        .verbose = options.verbose(),
        .node_kind = "app",
    });
    if (!state.ok) return state.status;

    const auto& app = state.project.targets[state.project.target[state.node]];
    const auto context = state.artifact_context();
    const auto executable = context.executable_path(app);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(executable, ec) || ec) {
        std::cerr << "debug: application is not built: " << executable.string()
                  << "; run build first\n";
        return mm::build::exit_run;
    }
    if (!state.toolchain.debugger) {
        std::cerr << "debug: "
                  << (state.target_lane ? "target " + state.toolchain.target : "host")
                  << " has no debugger; rerun configure with --debugger gdb\n";
        return mm::build::exit_run;
    }
    if (state.target_lane && !state.toolchain.runner) {
        std::cerr << "debug: target " << state.toolchain.target << " has no runner\n";
        return mm::build::exit_run;
    }

    if (options.verbose()) {
        std::cout << "modules.cpp debug tool\n";
        std::cout << "  app      " << app.name << "\n";
        std::cout << "  debugger " << state.toolchain.debugger->invocation << "\n";
        std::cout << "  target   " << executable.string() << "\n";
    }
    const int status = mm::debug::execute(state.toolchain, state.target_lane, executable,
                                          options.trailing());
    return status < 0 ? mm::build::exit_run : status;
}
