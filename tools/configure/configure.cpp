// modules.cpp configure tool
//
// Usage: configure [-v] [-e NAME=VALUE]... [<path to mm.mdy>] <command>
//        (default manifest: mm.mdy in the current dir)
//
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

import mm.app;
import mm.build;
import mm.configure;

namespace {

// Splits "NAME=VALUE" for -e. Empty name on a malformed argument.
struct Assignment {
    std::string_view name;
    std::string_view value;
};

Assignment split_assignment(std::string_view text) {
    const auto pos = text.find('=');
    if (pos == std::string_view::npos) return {};
    return {text.substr(0, pos), text.substr(pos + 1)};
}

}  // namespace

int main(int argc, char** argv) {
    // Two positionals: the command, optionally preceded by a manifest.
    mm::app::Options options("configure");
    options.option("-e", "a NAME=VALUE argument");
    options.positional_limit(2);
    if (options.parse(argc, argv) != mm::app::Cli::ok) return mm::build::exit_usage;

    const bool verbose = options.verbose();
    const auto assignments = options.values("-e");
    const auto& positional = options.positional();

    std::filesystem::path manifest_path;
    std::string_view command;

    if (positional.size() == 1) {
        manifest_path = "mm.mdy";
        command = positional[0];
    } else if (positional.size() == 2) {
        manifest_path = positional[0];
        command = positional[1];
    } else {
        std::cerr << "usage: configure [-v] [-e NAME=VALUE]... [<path to mm.mdy>] <command>\n";
        return mm::build::exit_usage;
    }

    manifest_path = mm::build::resolve_manifest(manifest_path);

    std::filesystem::path manifest_root;
    if (const auto status = mm::app::open_manifest("configure", manifest_path, manifest_root, true);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage : mm::build::exit_manifest;

    const auto project_root = mm::build::find_project_root(manifest_root);
    if (project_root.empty()) {
        std::cerr << "configure: no kind: project manifest above " << manifest_root.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto configuration_path = project_root / "out" / "config.mdy";

    std::error_code ec;
    std::filesystem::current_path(project_root, ec);
    if (ec) {
        std::cerr << "configure: cannot enter " << project_root.string() << ": " << ec.message()
                  << "\n";
        return mm::build::exit_manifest;
    }

    if (verbose) {
        std::cout << "modules.cpp configure tool\n";
        std::cout << "  root   " << project_root.string() << "\n";
        std::cout << "  config " << configuration_path.string() << "\n";
    }

    for (const auto assignment_text : assignments) {
        const auto assignment = split_assignment(assignment_text);
        if (assignment.name.empty()) {
            std::cerr << "configure: malformed -e argument: " << assignment_text << "\n";
            return mm::build::exit_usage;
        }
        if (!mm::configure::set(assignment.name, assignment.value)) {
            std::cerr << "configure: failed to set " << assignment.name << "\n";
            return mm::build::exit_run;
        }
        if (verbose)
            std::cout << "  export " << assignment.name << "=" << assignment.value << "\n";
    }

    const auto toolchain = mm::build::default_toolchain(verbose);

    mm::configure::Settings settings;
    settings.host.invocation = mm::configure::get("CXX").value_or("c++");
    settings.host.target = "host";
    settings.host.platform = "POSIX";
    settings.host.compile_flags = "-std=c++20 -fmodules-ts -x c++";
    settings.host.link_flags = toolchain.ldflags;

    if (!mm::configure::write_configuration(project_root, settings)) {
        std::cerr << "configure: failed to write " << configuration_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    if (verbose) std::cout << '\n';

    const int status = mm::build::run(toolchain, std::string(command));
    if (status < 0) {
        std::cerr << "configure: failed to run command\n";
        return mm::build::exit_run;
    }

    return status;
}
