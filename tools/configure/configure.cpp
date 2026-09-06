// modules.cpp configure tool
//
// Usage: configure [-v] [--compiler COMPILER] [--build debug|release]
//                  [<path to mm.mdy>]
//        (defaults: compiler gcc, build debug, manifest mm.mdy in the current dir)
//
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

import mm.app;
import mm.build;
import mm.configure;

int main(int argc, char** argv) {
    mm::app::Options options("configure");
    options.option("--compiler", "gcc, g++, clang, or clang++, optionally versioned");
    options.option("--build", "debug or release");
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

    const auto builds = options.values("--build");
    if (builds.size() > 1) {
        std::cerr << "configure: --build may be given only once\n";
        return mm::build::exit_usage;
    }

    const std::string requested_build = builds.empty() ? std::string("debug") : builds.front();
    const auto build = mm::configure::parse_build(requested_build);
    if (!build) {
        std::cerr << "configure: unsupported build: " << requested_build << "\n";
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

    const auto project = mm::build::load_project(".", {.tool = "configure", .strict_tree = true});
    if (!project.ok) return mm::build::exit_manifest;
    const auto requested_root = std::filesystem::canonical(manifest_root, ec);
    if (ec) return mm::build::exit_manifest;
    bool found = false;
    for (const auto& node : project.nodes) {
        const auto directory = std::filesystem::canonical(node.dir, ec);
        if (ec) return mm::build::exit_manifest;
        if (directory == requested_root) found = true;
    }
    if (!found) {
        std::cerr << "configure: requested manifest is not in the project tree: "
                  << manifest_root.string() << "\n";
        return mm::build::exit_manifest;
    }
    const auto nodes = mm::build::configuration_nodes(project);
    std::vector<mm::configure::OptionValues> resolved;
    if (!mm::configure::resolve_options(project_root, *build, nodes, resolved))
        return mm::build::exit_manifest;
    if (!mm::build::validate_capabilities(nodes, resolved, "configure"))
        return mm::build::exit_manifest;

    mm::configure::Settings settings;
    settings.name = requested + "-" + requested_build;
    settings.build = *build;
    settings.host.family = compiler->family;
    settings.host.invocation = compiler->invocation;
    settings.host.target = "host";
    settings.host.platform = "POSIX";
    settings.host.compile_flags = mm::configure::build_compile_flags(*build);
    settings.host.link_flags = mm::configure::build_link_flags(*build);
    settings.host_build_directory = mm::configure::host_output_directory();
    // Native: one compiler, one lane, so the target artifacts are the host's.
    settings.target_build_directory = mm::configure::host_output_directory();

    if (!mm::configure::write_configuration(project_root, settings)) {
        std::cerr << "configure: failed to write " << configuration_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    if (!mm::configure::write_option_records(project_root, settings.target_build_directory, *build,
                                             nodes, resolved, verbose)) {
        std::cerr << "configure: incomplete option snapshot; rerun configure\n";
        return mm::build::exit_manifest;
    }

    std::cout << "Configured " << mm::configure::build_name(*build) << " build with "
              << mm::configure::compiler_family_name(compiler->family) << " compiler "
              << compiler->invocation << "\n";
    return mm::build::exit_ok;
}
