// modules.cpp configure tool
//
// Usage: configure [-v|--verbose] [-h|--help] [--host | --target TRIPLE]
//                  [--target-host]
//                  [--compiler COMPILER] [--runner none|PROFILE]
//                  [--build debug|release]
//                  [<path to mm.mdy>]
//        (defaults: host lane, compiler gcc, build debug, and the current
//         directory's mm.mdy; a target lane defaults to TRIPLE-g++)
//
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import mm.app;
import mm.build;
import mm.configure;

namespace {

bool available_program(std::string_view invocation) {
    const auto executable = [](const std::filesystem::path& path) {
        std::error_code ec;
        const auto status = std::filesystem::status(path, ec);
        if (ec || !std::filesystem::is_regular_file(status)) return false;
        const auto execute = std::filesystem::perms::owner_exec |
                             std::filesystem::perms::group_exec |
                             std::filesystem::perms::others_exec;
        return (status.permissions() & execute) != std::filesystem::perms::none;
    };
    const std::filesystem::path program(invocation);
    if (program.has_parent_path()) return executable(program);
    const char* raw_path = std::getenv("PATH");
    if (raw_path == nullptr) return false;
    std::string_view path(raw_path);
    for (std::size_t begin = 0; begin <= path.size();) {
        const auto end = path.find(':', begin);
        const auto part = path.substr(begin, end == std::string_view::npos ? path.size() - begin
                                                                          : end - begin);
        const auto directory = part.empty() ? std::filesystem::path(".")
                                            : std::filesystem::path(part);
        if (executable(directory / program)) return true;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return false;
}

std::string compile_flags(mm::configure::Build build, mm::configure::CompilerFamily family,
                          std::string_view target) {
    std::string flags(mm::configure::build_compile_flags(build));
    if (family == mm::configure::CompilerFamily::Clang && target != "host")
        flags += " --target=" + std::string(target);
    return flags;
}

std::string link_flags(mm::configure::Build build, mm::configure::CompilerFamily family,
                       std::string_view target) {
    std::string flags(mm::configure::build_link_flags(build));
    if (family == mm::configure::CompilerFamily::Clang && target != "host")
        flags += " --target=" + std::string(target);
    return flags;
}

mm::configure::CompilerSettings compiler_settings(const mm::build::Toolchain& toolchain,
                                                   mm::configure::Build build) {
    return {
        toolchain.family,
        toolchain.compiler.invocation,
        toolchain.target,
        "POSIX",
        compile_flags(build, toolchain.family, toolchain.target),
        link_flags(build, toolchain.family, toolchain.target),
    };
}

std::optional<mm::configure::RunnerSettings> runner_settings(
    const mm::build::Toolchain& toolchain) {
    if (!toolchain.runner) return std::nullopt;
    const auto& source = *toolchain.runner;
    return mm::configure::RunnerSettings{source.invocation, source.prefix_arguments,
                                         source.image, source.image_option,
                                         source.suffix_arguments, source.forwards_arguments};
}

}

int main(int argc, char** argv) {
    mm::app::Options options("configure");
    options.flag("--host");
    options.flag("--target-host");
    options.option("--target", "a target triple");
    options.option("--compiler", "a native or target-prefixed GCC or Clang C++ driver");
    options.option("--runner", "none or a supported runner profile");
    options.option("--build", "debug or release");
    options.help("configure [-v|--verbose] [-h|--help] [--host | --target TRIPLE] "
                 "[--target-host] "
                 "[--compiler COMPILER] [--runner none|PROFILE] "
                 "[--build debug|release] [manifest]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;

    const bool verbose = options.verbose();
    const auto targets = options.values("--target");
    if (options.count("--host") > 1 || options.count("--target-host") > 1 ||
        targets.size() > 1) {
        std::cerr << "configure: lane option may be given only once\n";
        return mm::build::exit_usage;
    }
    if (options.seen("--host") && !targets.empty()) {
        std::cerr << "configure: --host and --target are mutually exclusive\n";
        return mm::build::exit_usage;
    }
    const bool target_lane = !targets.empty();
    const auto runners = options.values("--runner");
    if (runners.size() > 1) {
        std::cerr << "configure: --runner may be given only once\n";
        return mm::build::exit_usage;
    }
    if ((options.seen("--target-host") || !runners.empty()) && !target_lane) {
        std::cerr << "configure: --target-host and --runner require --target TRIPLE\n";
        return mm::build::exit_usage;
    }
    const std::string target = target_lane ? targets.front() : std::string("host");
    if (target_lane && !mm::configure::valid_target_triple(target)) {
        std::cerr << "configure: invalid target triple: " << target << "\n";
        return mm::build::exit_usage;
    }

    const auto compilers = options.values("--compiler");
    if (compilers.size() > 1) {
        std::cerr << "configure: --compiler may be given only once\n";
        return mm::build::exit_usage;
    }

    const std::string requested = compilers.empty()
                                      ? (target_lane ? target + "-g++" : std::string("gcc"))
                                      : compilers.front();
    const auto compiler = mm::configure::parse_compiler(requested);
    if (!compiler) {
        std::cerr << "configure: unsupported compiler: " << requested << "\n";
        return mm::build::exit_usage;
    }
    if ((!target_lane && !compiler->target_prefix.empty()) ||
        (target_lane && compiler->family == mm::configure::CompilerFamily::Gcc &&
         compiler->target_prefix != target) ||
        (target_lane && !compiler->target_prefix.empty() && compiler->target_prefix != target)) {
        std::cerr << "configure: compiler " << requested << " is not compatible with "
                  << target << "\n";
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
    const bool has_configuration = std::filesystem::exists(configuration_path, ec);
    if (ec) return mm::build::exit_manifest;
    if (has_configuration) {
        mm::build::BuildConfiguration existing;
        if (!mm::build::load_configuration(configuration_path, verbose, existing))
            return mm::build::exit_manifest;
        settings.host = compiler_settings(existing.host_toolchain(), *build);
        if (const auto* cross = existing.cross_toolchain()) {
            settings.cross = compiler_settings(*cross, *build);
            settings.target_build_directory = *existing.cross_build_directory();
            settings.target_has_host_capability =
                existing.target_has_host_capability();
            settings.cross_runner = runner_settings(*cross);
        }
    } else {
        settings.host = mm::configure::CompilerSettings{
            mm::configure::CompilerFamily::Gcc,
            "g++",
            "host",
            "POSIX",
            std::string(mm::configure::build_compile_flags(*build)),
            std::string(mm::configure::build_link_flags(*build)),
        };
    }

    settings.name = (target_lane ? target : compiler->invocation) + "-" + requested_build;
    settings.build = *build;
    settings.host_build_directory = mm::configure::host_output_directory();
    if (target_lane) {
        settings.target_compiler = mm::configure::CompilerSelection::Cross;
        settings.target_has_host_capability = options.seen("--target-host");
        settings.cross = mm::configure::CompilerSettings{
            compiler->family,
            compiler->invocation,
            target,
            "POSIX",
            compile_flags(*build, compiler->family, target),
            link_flags(*build, compiler->family, target),
        };
        settings.cross_runner.reset();
        if (!runners.empty() && runners.front() != "none") {
            settings.cross_runner = mm::configure::runner_profile(runners.front(), target);
            if (!settings.cross_runner) {
                std::cerr << "configure: runner " << runners.front()
                          << " is not compatible with " << target << "\n";
                return mm::build::exit_usage;
            }
            if (!available_program(settings.cross_runner->invocation)) {
                std::cerr << "configure: runner is not available: "
                          << settings.cross_runner->invocation << "\n";
                return mm::build::exit_usage;
            }
        }
        settings.target_build_directory = mm::configure::target_output_directory(target);
    } else {
        settings.target_compiler = mm::configure::CompilerSelection::Host;
        settings.host = mm::configure::CompilerSettings{
            compiler->family,
            compiler->invocation,
            "host",
            "POSIX",
            std::string(mm::configure::build_compile_flags(*build)),
            std::string(mm::configure::build_link_flags(*build)),
        };
        if (!settings.cross)
            settings.target_build_directory = mm::configure::host_output_directory();
    }

    if (!mm::configure::write_configuration(project_root, settings)) {
        std::cerr << "configure: failed to write " << configuration_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto selected_output = target_lane ? settings.target_build_directory
                                             : settings.host_build_directory;
    if (!mm::configure::write_option_records(project_root, selected_output, *build,
                                             nodes, resolved, verbose)) {
        std::cerr << "configure: incomplete option snapshot; rerun configure\n";
        return mm::build::exit_manifest;
    }

    std::cout << "Configured " << (target_lane ? target : std::string("host")) << " "
              << mm::configure::build_name(*build) << " build with "
              << mm::configure::compiler_family_name(compiler->family) << " compiler "
              << compiler->invocation << "\n";
    if (verbose && target_lane)
        std::cout << "  runner "
                  << (settings.cross_runner ? settings.cross_runner->invocation : "none") << "\n";
    return mm::build::exit_ok;
}
