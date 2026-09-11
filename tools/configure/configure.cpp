// modules.cpp configure tool
//
// Usage: configure [-v|--verbose] [-h|--help] [--host | --target TRIPLE]
//                  [--target-host]
//                  [--compiler COMPILER] [--runner none|PROFILE]
//                  [--debugger none|gdb]
//                  [--build debug|release]
//                  [<path to mm.mdy>]
//        (defaults: host lane, compiler gcc, build debug, and the current
//         directory's mm.mdy; a target lane defaults to TRIPLE-g++)
//
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdlib>
#include <cstdio>
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
        toolchain.c_compiler.invocation,
    };
}

std::optional<mm::configure::RunnerSettings> runner_settings(
    const mm::build::Toolchain& toolchain) {
    if (!toolchain.runner) return std::nullopt;
    const auto& source = *toolchain.runner;
    return mm::configure::RunnerSettings{
        .invocation = source.invocation,
        .prefix_arguments = source.prefix_arguments,
        .image = source.image,
        .image_option = source.image_option,
        .image_arguments = source.image_arguments,
        .suffix_arguments = source.suffix_arguments,
        .forwards_arguments = source.forwards_arguments,
    };
}

std::optional<mm::configure::DebuggerSettings> debugger_settings(
    const mm::build::Toolchain& toolchain) {
    if (!toolchain.debugger) return std::nullopt;
    const auto& source = *toolchain.debugger;
    return mm::configure::DebuggerSettings{
        source.invocation, source.prefix_arguments, source.connection,
        source.remote_endpoint, source.runner_arguments};
}

bool existing_directory(const std::optional<std::filesystem::path>& path) {
    if (!path) return true;
    std::error_code ec;
    return std::filesystem::is_directory(*path, ec) && !ec;
}

bool probe_specs(std::string_view compiler, std::string_view profile) {
    const std::string query = std::string(profile) + ".specs";
    const std::string command = mm::build::shell_quote(std::filesystem::path(compiler)) +
                                " -print-file-name=" +
                                mm::build::shell_quote(std::filesystem::path(query));
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) return false;
    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    const int status = ::pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' ||
                              output.back() == ' ' || output.back() == '\t'))
        output.pop_back();
    if (status != 0 || output.empty() || output == query) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(output, ec) && !ec;
}

const mm::build::SdkDefinition* find_sdk(const mm::build::Project& project,
                                         std::string_view name) {
    for (const auto& sdk : project.sdks)
        if (sdk.name == name) return &sdk;
    return nullptr;
}

const mm::build::BoardDefinition* find_board(const mm::build::Project& project,
                                             std::string_view name) {
    for (const auto& board : project.boards)
        if (board.name == name) return &board;
    return nullptr;
}

const mm::build::LibraryDefinition* find_library(const mm::build::Project& project,
                                                 std::string_view name) {
    for (const auto& library : project.libraries)
        if (library.name == name) return &library;
    return nullptr;
}

int resolve_platform(const mm::build::Project& project,
                     std::string_view sdk_name, std::string_view board_name,
                     std::string_view target, mm::configure::CompilerFamily family,
                     std::string_view compiler,
                     mm::configure::PlatformSettings& platform) {
    const mm::build::BoardDefinition* board = nullptr;
    if (!board_name.empty()) {
        board = find_board(project, board_name);
        if (board == nullptr) {
            std::cerr << "configure: unknown board: " << board_name << "\n";
            return mm::build::exit_usage;
        }
        if (!sdk_name.empty() && sdk_name != board->sdk) {
            std::cerr << "configure: board " << board_name << " uses SDK " << board->sdk
                      << ", not " << sdk_name << "\n";
            return mm::build::exit_usage;
        }
        sdk_name = board->sdk;
    }
    const auto* sdk = find_sdk(project, sdk_name);
    if (sdk == nullptr) {
        std::cerr << "configure: unknown SDK: " << sdk_name << "\n";
        return mm::build::exit_usage;
    }
    if (sdk->target != target || sdk->family != family) {
        std::cerr << "configure: SDK " << sdk->name << " is not compatible with " << target
                  << " and " << mm::configure::compiler_family_name(family) << "\n";
        return mm::build::exit_usage;
    }
    const mm::build::LibraryDefinition* library = nullptr;
    if (!sdk->library.empty()) {
        library = find_library(project, sdk->library);
        if (library == nullptr || !mm::build::validate_library_checkout(".", *library))
            return mm::build::exit_manifest;
    }
    if (!existing_directory(sdk->sysroot) || !existing_directory(sdk->runtime_prefix)) {
        std::cerr << "configure: SDK " << sdk->name
                  << " names an installed directory that does not exist\n";
        return mm::build::exit_manifest;
    }
    if (!sdk->specs_profile.empty() && !probe_specs(compiler, sdk->specs_profile)) {
        std::cerr << "configure: compiler cannot resolve specs profile "
                  << sdk->specs_profile << "\n";
        return mm::build::exit_manifest;
    }

    platform = {};
    platform.target = std::string(target);
    platform.system = *mm::configure::target_system(target);
    platform.runtime = sdk->runtime;
    if (library != nullptr && !library->external_build.empty()) {
        platform.link_ownership = mm::configure::LinkOwnership::External;
    }
    platform.sdk = sdk->name;
    platform.sdk_manifest = sdk->manifest;
    platform.sdk_family = sdk->family;
    platform.sysroot = sdk->sysroot;
    platform.runtime_prefix = sdk->runtime_prefix;
    if (!sdk->specs_profile.empty())
        platform.specs_argument = "--specs=" + sdk->specs_profile + ".specs";
    else if (!sdk->specs_file.empty())
        platform.specs_argument = "--specs=" + mm::build::shell_quote(sdk->specs_file);

    for (const auto responsibility : sdk->provides)
        platform.responsibility_owners[responsibility] = sdk->name;
    if (!sdk->specs_profile.empty()) {
        platform.responsibility_owners[mm::configure::Responsibility::RuntimeInit] = sdk->name;
        platform.responsibility_owners[mm::configure::Responsibility::Syscalls] = sdk->name;
    }

    if (board != nullptr) {
        platform.board = board->name;
        platform.board_manifest = board->manifest;
        platform.machine = board->machine;
        if (platform.link_ownership != mm::configure::LinkOwnership::External) {
            platform.linker_script = board->linker_script;
            platform.board_sources = board->sources;
        }
        platform.compiler_arguments = {"-mcpu=" + board->cpu};
        if (board->instruction_set == "thumb") platform.compiler_arguments.push_back("-mthumb");
        platform.compiler_arguments.push_back("-mfloat-abi=" + board->float_abi);
        for (const auto responsibility : board->provides) {
            const auto found = platform.responsibility_owners.find(responsibility);
            if (found != platform.responsibility_owners.end()) {
                std::cerr << "configure: responsibility "
                          << mm::configure::responsibility_name(responsibility)
                          << " is provided by both " << found->second << " and " << board->name
                          << "\n";
                return mm::build::exit_manifest;
            }
            platform.responsibility_owners[responsibility] = board->name;
        }
    }

    if (platform.system == mm::configure::PlatformSystem::BareMetal) {
        platform.models_responsibilities = true;
        for (const auto responsibility : {
                 mm::configure::Responsibility::ResetVector,
                 mm::configure::Responsibility::InitialStack,
                 mm::configure::Responsibility::MemoryLayout,
                 mm::configure::Responsibility::RuntimeInit,
                 mm::configure::Responsibility::Syscalls}) {
            if (!platform.responsibility_owners.contains(responsibility))
                platform.unresolved.push_back(responsibility);
        }
        if (board != nullptr && !platform.unresolved.empty()) {
            std::cerr << "configure: board " << board->name << " leaves responsibility "
                      << mm::configure::responsibility_name(platform.unresolved.front())
                      << " unresolved\n";
            return mm::build::exit_manifest;
        }
        if (!platform.responsibility_owners.contains(mm::configure::Responsibility::RuntimeInit) ||
            !platform.responsibility_owners.contains(mm::configure::Responsibility::Syscalls)) {
            std::cerr << "configure: SDK " << sdk->name
                      << " does not supply runtime-init and syscalls\n";
            return mm::build::exit_manifest;
        }
    }
    return mm::build::exit_ok;
}

}

int main(int argc, char** argv) {
    mm::app::Options options("configure");
    options.flag("--host");
    options.flag("--target-host");
    options.option("--target", "a target triple");
    options.option("--compiler", "a native or target-prefixed GCC or Clang C++ driver");
    options.option("--c-compiler", "a native or target-prefixed GCC or Clang C driver");
    options.option("--runner", "none or a supported runner profile");
    options.option("--sdk", "a supported SDK manifest name");
    options.option("--board", "a supported board manifest name");
    options.option("--debugger", "none, gdb, or openocd");
    options.option("--build", "debug or release");
    options.help("configure [-v|--verbose] [-h|--help] [--host | --target TRIPLE] "
                 "[--target-host] "
                 "[--compiler COMPILER] [--c-compiler C_COMPILER] [--sdk SDK] [--board BOARD] "
                 "[--runner none|PROFILE] [--debugger none|gdb|openocd] "
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
    const auto sdks = options.values("--sdk");
    const auto boards = options.values("--board");
    if (sdks.size() > 1 || boards.size() > 1) {
        std::cerr << "configure: --sdk and --board may each be given only once\n";
        return mm::build::exit_usage;
    }
    const auto runners = options.values("--runner");
    if (runners.size() > 1) {
        std::cerr << "configure: --runner may be given only once\n";
        return mm::build::exit_usage;
    }
    const auto debuggers = options.values("--debugger");
    if (debuggers.size() > 1) {
        std::cerr << "configure: --debugger may be given only once\n";
        return mm::build::exit_usage;
    }
    if ((options.seen("--target-host") || !runners.empty() || !sdks.empty() ||
         !boards.empty()) && !target_lane) {
        std::cerr << "configure: --target-host, --sdk, --board, and --runner require --target TRIPLE\n";
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

    const auto c_compilers = options.values("--c-compiler");
    if (c_compilers.size() > 1) {
        std::cerr << "configure: --c-compiler may be given only once\n";
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
    if (verbose) {
        for (const auto& library : project.libraries)
            std::cout << "  library " << library.name << " checkout "
                      << (library.checkout_present ? "present" : "absent") << "\n";
    }
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
    const auto structural = mm::build::structural_properties(resolved);
    if (!mm::build::validate_structural_properties(nodes, structural, "configure"))
        return mm::build::exit_manifest;

    std::optional<mm::configure::PlatformSettings> selected_platform;
    if (target_lane) {
        if (!mm::configure::target_system(target)) {
            std::cerr << "configure: unsupported target platform: " << target << "\n";
            return mm::build::exit_usage;
        }
        if (sdks.empty() && boards.empty()) {
            std::cerr << "configure: target " << target
                      << " requires --sdk or --board; compatible SDKs:";
            bool any = false;
            for (const auto& sdk : project.sdks) {
                if (sdk.target != target || sdk.family != compiler->family) continue;
                std::cerr << (any ? ", " : " ") << sdk.name;
                any = true;
            }
            if (!any) std::cerr << " none";
            std::cerr << "\n";
            return mm::build::exit_usage;
        }
        mm::configure::PlatformSettings platform;
        if (const int status = resolve_platform(
                project, sdks.empty() ? std::string_view{} : sdks.front(),
                boards.empty() ? std::string_view{} : boards.front(), target,
                compiler->family, compiler->invocation, platform);
            status != mm::build::exit_ok)
            return status;
        selected_platform = std::move(platform);
    }

    mm::configure::Settings settings;
    const bool has_configuration = std::filesystem::exists(configuration_path, ec);
    if (ec) return mm::build::exit_manifest;
    if (has_configuration) {
        mm::build::BuildConfiguration existing;
        if (!mm::build::load_configuration(configuration_path, verbose, existing))
            return mm::build::exit_manifest;
        settings.configuration_2 = existing.configuration_2();
        settings.host_platform = existing.host_platform();
        settings.host = compiler_settings(existing.host_toolchain(), *build);
        settings.host_debugger = debugger_settings(existing.host_toolchain());
        if (const auto* cross = existing.cross_toolchain()) {
            settings.cross = compiler_settings(*cross, *build);
            settings.target_build_directory = *existing.cross_build_directory();
            settings.target_has_host_capability =
                existing.target_has_host_capability();
            settings.cross_runner = runner_settings(*cross);
            settings.cross_debugger = debugger_settings(*cross);
            if (const auto* platform = existing.configured_target_platform())
                settings.cross_platform = *platform;
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

    bool external_lane = false;
    if (target_lane && selected_platform && selected_platform->sdk) {
        if (const auto* sdk = find_sdk(project, *selected_platform->sdk)) {
            if (!sdk->library.empty()) {
                if (const auto* lib = find_library(project, sdk->library)) {
                    external_lane = !lib->external_build.empty();
                }
            }
        }
    }

    std::string c_driver;
    const auto expected_family = target_lane ? (selected_platform ? selected_platform->sdk_family : compiler->family)
                                             : compiler->family;
    if (!c_compilers.empty()) {
        c_driver = c_compilers.front();
        std::string err;
        if (!mm::configure::probe_c_compiler(c_driver, compiler->invocation, expected_family, err)) {
            std::cerr << "configure: " << err << "\n";
            return mm::build::exit_usage;
        }
    } else {
        const auto candidate = mm::configure::candidate_c_compiler(compiler->invocation);
        std::string err;
        if (!candidate.empty() && mm::configure::probe_c_compiler(candidate, compiler->invocation, expected_family, err)) {
            c_driver = candidate;
        } else if (external_lane) {
            std::cerr << "configure: external build requires a compatible C driver: "
                      << (candidate.empty() ? "cannot derive candidate C driver for " + compiler->invocation : err)
                      << "\n";
            return mm::build::exit_manifest;
        }
    }

    settings.name = (target_lane ? target : compiler->invocation) + "-" + requested_build;
    settings.build = *build;
    settings.host_build_directory = mm::configure::host_output_directory();
    if (target_lane) {
        settings.configuration_2 = true;
        settings.cross_platform = selected_platform;
        settings.target_compiler = mm::configure::CompilerSelection::Cross;
        settings.target_has_host_capability = options.seen("--target-host");
        settings.cross = mm::configure::CompilerSettings{
            compiler->family,
            compiler->invocation,
            target,
            "POSIX",
            compile_flags(*build, compiler->family, target),
            link_flags(*build, compiler->family, target),
            c_driver,
        };
        settings.cross_runner.reset();
        if (!runners.empty() && runners.front() != "none") {
            settings.cross_runner = mm::configure::runner_profile(
                runners.front(), target, &*settings.cross_platform);
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
        settings.cross_debugger.reset();
        if (!debuggers.empty() && debuggers.front() != "none") {
            settings.cross_debugger = mm::configure::debugger_profile(
                debuggers.front(), target,
                settings.cross_platform ? &*settings.cross_platform : nullptr);
            if (!settings.cross_debugger) {
                std::cerr << "configure: debugger " << debuggers.front()
                          << " is not compatible with " << target << "\n";
                return mm::build::exit_usage;
            }
            if (!settings.cross_runner &&
                settings.cross_debugger->connection ==
                    mm::configure::DebuggerConnection::RunnerRemote) {
                std::cerr << "configure: debugger " << debuggers.front()
                          << " requires a configured target runner\n";
                return mm::build::exit_usage;
            }
            if (!available_program(settings.cross_debugger->invocation)) {
                std::cerr << "configure: debugger is not available: "
                          << settings.cross_debugger->invocation << "\n";
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
            c_driver,
        };
        settings.host_debugger.reset();
        if (!debuggers.empty() && debuggers.front() != "none") {
            settings.host_debugger =
                mm::configure::debugger_profile(debuggers.front(), "host");
            if (!settings.host_debugger) {
                std::cerr << "configure: unsupported debugger: " << debuggers.front() << "\n";
                return mm::build::exit_usage;
            }
            if (!available_program(settings.host_debugger->invocation)) {
                std::cerr << "configure: debugger is not available: "
                          << settings.host_debugger->invocation << "\n";
                return mm::build::exit_usage;
            }
        }
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
    if (verbose)
        std::cout << "  debugger "
                  << ((target_lane ? settings.cross_debugger : settings.host_debugger)
                          ? (target_lane ? settings.cross_debugger->invocation
                                         : settings.host_debugger->invocation)
                          : "none")
                  << "\n";
    return mm::build::exit_ok;
}
