// modules.cpp configure tool
//
// Usage: configure [-v] [--compiler COMPILER] [<path to mm.mdy>]
//        (default compiler: gcc; default manifest: mm.mdy in the current dir)
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

int main(int argc, char** argv) {
    mm::app::Options options("configure");
    options.option("--compiler", "gcc, g++, clang, or clang++, optionally versioned");
    if (options.parse(argc, argv) != mm::app::Cli::ok) return mm::build::exit_usage;

    const bool verbose = options.verbose();
    const auto compilers = options.values("--compiler");
    if (compilers.size() > 1) {
        std::cerr << "configure: --compiler may be given only once\n";
        return mm::build::exit_usage;
    }

    const std::string requested = compilers.empty() ? std::string("gcc") : compilers.front();
    const auto compiler = mm::configure::parse_compiler(requested);
    if (!compiler) {
        std::cerr << "configure: unsupported compiler: " << requested << "\n";
        return mm::build::exit_usage;
    }

    std::filesystem::path manifest_path = options.positional().empty()
                                              ? std::filesystem::path("mm.mdy")
                                              : std::filesystem::path(options.positional().front());

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

    mm::configure::Settings settings;
    settings.name = requested;
    settings.host.family = compiler->family;
    settings.host.invocation = compiler->invocation;
    settings.host.target = "host";
    settings.host.platform = "POSIX";
    settings.host.compile_flags = "-std=c++20";
    settings.host.link_flags = "-std=c++20";

    if (!mm::configure::write_configuration(project_root, settings)) {
        std::cerr << "configure: failed to write " << configuration_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    std::cout << "Configured " << mm::configure::compiler_family_name(compiler->family)
              << " compiler " << compiler->invocation << "\n";
    return mm::build::exit_ok;
}
