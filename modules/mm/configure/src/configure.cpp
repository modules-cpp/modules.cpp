// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

module mm.configure;

namespace mm::configure {

std::string_view platform_system_name(PlatformSystem system) {
    switch (system) {
        case PlatformSystem::Posix: return "POSIX";
        case PlatformSystem::Linux: return "linux";
        case PlatformSystem::BareMetal: return "bare-metal";
        case PlatformSystem::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view platform_runtime_name(PlatformRuntime runtime) {
    switch (runtime) {
        case PlatformRuntime::Unknown: return "unknown";
        case PlatformRuntime::Glibc: return "glibc";
        case PlatformRuntime::Newlib: return "newlib";
        case PlatformRuntime::Picolibc: return "picolibc";
        case PlatformRuntime::None: return "none";
    }
    return "unknown";
}

std::string_view responsibility_name(Responsibility responsibility) {
    switch (responsibility) {
        case Responsibility::ResetVector: return "reset-vector";
        case Responsibility::InitialStack: return "initial-stack";
        case Responsibility::MemoryLayout: return "memory-layout";
        case Responsibility::RuntimeInit: return "runtime-init";
        case Responsibility::Syscalls: return "syscalls";
    }
    return {};
}

std::optional<PlatformSystem> target_system(std::string_view target) {
    if (target == "m68k-linux-gnu" || target == "aarch64-linux-gnu" ||
        target == "arm-linux-gnueabihf")
        return PlatformSystem::Linux;
    if (target == "arm-none-eabi") return PlatformSystem::BareMetal;
    return std::nullopt;
}

namespace {

bool valid_scalar(std::string_view value, bool allow_empty = false) {
    return (allow_empty || !value.empty()) && value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos && value.find('\0') == std::string_view::npos;
}

bool contained(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) return false;
    const auto relative = canonical.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

bool valid_build_directory(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;

    const auto normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& component : normalized)
        if (component == "..") return false;

    return valid_scalar(normalized.generic_string());
}

bool safe_output(const std::filesystem::path& root, const std::filesystem::path& directory,
                 const std::filesystem::path& path) {
    if (!valid_build_directory(directory)) return false;
    std::error_code ec;
    const auto canonical_root = std::filesystem::canonical(root, ec);
    if (ec) return false;
    // Keep the output boundary anchored to the project, not to the target of a
    // user-planted output symlink (even a target elsewhere inside the project).
    const auto output = canonical_root / directory;
    return contained(output, root / directory) && contained(output, path);
}

// An exclusively created temporary directory avoids following a pre-existing
// temporary-file symlink. Rename publishes only a complete file.
bool write_atomic(const std::filesystem::path& root, const std::filesystem::path& directory,
                  const std::filesystem::path& path, std::string_view contents) {
    if (!safe_output(root, directory, path)) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec || !safe_output(root, directory, path)) return false;
    std::filesystem::path temporary_dir;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto candidate = path.parent_path() /
            (".configure-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(candidate, ec)) {
            temporary_dir = candidate;
            break;
        }
        if (ec) return false;
    }
    if (temporary_dir.empty()) return false;
    const auto temporary = temporary_dir / "record";
    std::ofstream out(temporary, std::ios::binary);
    out << contents;
    out.close();
    bool ok = static_cast<bool>(out) && safe_output(root, directory, path);
    if (ok) {
        std::filesystem::rename(temporary, path, ec);
        ok = !ec;
    }
    std::filesystem::remove(temporary, ec);
    std::filesystem::remove(temporary_dir, ec);
    return ok;
}

bool valid_compiler(const CompilerSettings& compiler, bool require_platform = true) {
    return valid_scalar(compiler.invocation) && valid_scalar(compiler.target) &&
           (!require_platform || valid_scalar(compiler.platform)) &&
           valid_scalar(compiler.compile_flags) &&
           valid_scalar(compiler.link_flags);
}

bool valid_platform(const PlatformSettings& platform, bool configured) {
    if (!valid_scalar(platform.target) || platform.target == "host") return !configured;
    if (!configured) return true;
    if (!platform.sdk || !platform.sdk_manifest ||
        !valid_scalar(*platform.sdk) || platform.sdk_manifest->empty())
        return false;
    if (platform.board.has_value() != platform.board_manifest.has_value()) return false;
    for (const auto& argument : platform.compiler_arguments)
        if (!valid_scalar(argument)) return false;
    return true;
}

bool valid_runner(const RunnerSettings& runner) {
    if (!valid_scalar(runner.invocation)) return false;
    for (const auto& argument : runner.prefix_arguments)
        if (!valid_scalar(argument, true)) return false;
    for (const auto& argument : runner.suffix_arguments)
        if (!valid_scalar(argument, true)) return false;
    for (const auto& argument : runner.image_arguments)
        if (!valid_scalar(argument, true)) return false;
    return runner.image == RunnerImage::Positional
               ? runner.image_option.empty() && runner.image_arguments.empty()
               : (!runner.image_option.empty() || !runner.image_arguments.empty());
}

bool valid_debugger(const DebuggerSettings& debugger) {
    if (!valid_scalar(debugger.invocation)) return false;
    for (const auto& argument : debugger.prefix_arguments)
        if (!valid_scalar(argument, true)) return false;
    for (const auto& argument : debugger.runner_arguments)
        if (!valid_scalar(argument, true)) return false;
    if (debugger.connection == DebuggerConnection::Direct)
        return debugger.remote_endpoint.empty() && debugger.runner_arguments.empty();
    return valid_scalar(debugger.remote_endpoint);
}

bool valid_settings(const Settings& settings) {
    if (!valid_scalar(settings.name) || !valid_scalar(build_name(settings.build)) ||
        !valid_compiler(settings.host) ||
        !valid_build_directory(settings.host_build_directory) ||
        !valid_build_directory(settings.target_build_directory))
        return false;

    if (settings.cross && !valid_compiler(*settings.cross, !settings.configuration_2)) return false;
    if (settings.host.target != "host" || settings.host_platform.target != "host" ||
        settings.host_platform.system != PlatformSystem::Posix)
        return false;
    if (settings.configuration_2 &&
        (!settings.cross || !settings.cross_platform ||
         !valid_platform(*settings.cross_platform, true)))
        return false;
    if (settings.host_debugger && !valid_debugger(*settings.host_debugger)) return false;
    if (settings.host_debugger &&
        settings.host_debugger->connection != DebuggerConnection::Direct)
        return false;
    if (settings.cross_debugger && (!settings.cross || !valid_debugger(*settings.cross_debugger)))
        return false;
    if (settings.cross_debugger &&
        settings.cross_debugger->connection != DebuggerConnection::RunnerRemote)
        return false;
    if (settings.cross_runner && (!settings.cross || !valid_runner(*settings.cross_runner)))
        return false;
    if (settings.cross_debugger &&
        settings.cross_debugger->connection == DebuggerConnection::RunnerRemote &&
        !settings.cross_runner)
        return false;
    if (settings.target_has_host_capability && !settings.cross) return false;
    if (settings.target_compiler == CompilerSelection::Cross) {
        if (!settings.cross) return false;
        if (settings.host_build_directory.lexically_normal() ==
            settings.target_build_directory.lexically_normal())
            return false;
    }

    return true;
}

void write_compiler(std::ostream& out, std::string_view prefix,
                    const CompilerSettings& compiler, bool write_platform = true) {
    out << prefix << "-compiler-family: " << compiler_family_name(compiler.family) << '\n';
    out << prefix << "-compiler: " << compiler.invocation << '\n';
    out << prefix << "-target: " << compiler.target << '\n';
    if (write_platform) out << prefix << "-platform: " << compiler.platform << '\n';
    out << prefix << "-compile-flags: " << compiler.compile_flags << '\n';
    out << prefix << "-link-flags: " << compiler.link_flags << '\n';
}

void write_platform(std::ostream& out, const PlatformSettings& platform) {
    out << "cross-system: " << platform_system_name(platform.system) << '\n';
    out << "cross-runtime: " << platform_runtime_name(platform.runtime) << '\n';
    out << "cross-sdk: " << *platform.sdk << '\n';
    out << "cross-sdk-manifest: " << platform.sdk_manifest->generic_string() << '\n';
    out << "cross-sdk-compiler-family: " << compiler_family_name(platform.sdk_family) << '\n';
    if (platform.sysroot) out << "cross-sdk-sysroot: " << platform.sysroot->generic_string() << '\n';
    if (platform.runtime_prefix)
        out << "cross-sdk-runtime-prefix: " << platform.runtime_prefix->generic_string() << '\n';
    if (!platform.specs_argument.empty())
        out << "cross-sdk-specs-argument: " << platform.specs_argument << '\n';
    for (const auto& [responsibility, owner] : platform.responsibility_owners)
        if (owner == *platform.sdk)
            out << "cross-sdk-provides: " << responsibility_name(responsibility) << '\n';
    if (platform.board) {
        out << "cross-board: " << *platform.board << '\n';
        out << "cross-board-manifest: " << platform.board_manifest->generic_string() << '\n';
        if (!platform.machine.empty()) out << "cross-board-machine: " << platform.machine << '\n';
        out << "cross-board-linker-script: " << platform.linker_script.generic_string() << '\n';
        for (const auto& source : platform.board_sources)
            out << "cross-board-source: " << source.generic_string() << '\n';
        for (const auto& [responsibility, owner] : platform.responsibility_owners)
            if (owner == *platform.board)
                out << "cross-board-provides: " << responsibility_name(responsibility) << '\n';
        for (const auto& argument : platform.compiler_arguments)
            out << "cross-board-argument: " << argument << '\n';
    }
    for (const auto responsibility : platform.unresolved)
        out << "cross-unresolved: " << responsibility_name(responsibility) << '\n';
}

void write_runner(std::ostream& out, const RunnerSettings& runner) {
    out << "cross-runner: " << runner.invocation << '\n';
    for (const auto& argument : runner.prefix_arguments)
        out << "cross-runner-prefix-argument: " << argument << '\n';
    out << "cross-runner-image: "
        << (runner.image == RunnerImage::Positional ? "positional" : "option") << '\n';
    if (runner.image == RunnerImage::Option) {
        if (!runner.image_option.empty())
            out << "cross-runner-image-option: " << runner.image_option << '\n';
        for (const auto& argument : runner.image_arguments)
            out << "cross-runner-image-argument: " << argument << '\n';
    }
    for (const auto& argument : runner.suffix_arguments)
        out << "cross-runner-suffix-argument: " << argument << '\n';
    out << "cross-runner-forwards-arguments: "
        << (runner.forwards_arguments ? "yes" : "no") << '\n';
}

void write_debugger(std::ostream& out, std::string_view prefix,
                    const DebuggerSettings& debugger) {
    out << prefix << "-debugger: " << debugger.invocation << '\n';
    for (const auto& argument : debugger.prefix_arguments)
        out << prefix << "-debugger-prefix-argument: " << argument << '\n';
    out << prefix << "-debugger-connection: "
        << (debugger.connection == DebuggerConnection::Direct ? "direct" : "runner-remote")
        << '\n';
    if (debugger.connection == DebuggerConnection::RunnerRemote) {
        out << prefix << "-debugger-remote-endpoint: " << debugger.remote_endpoint << '\n';
        for (const auto& argument : debugger.runner_arguments)
            out << prefix << "-debugger-runner-argument: " << argument << '\n';
    }
}

}  // namespace

std::optional<CompilerRequest> parse_compiler(std::string_view value) {
    CompilerRequest result;
    std::string_view suffix;

    if (value.starts_with("clang++")) {
        result.family = CompilerFamily::Clang;
        result.invocation = "clang++";
        suffix = value.substr(7);
    } else if (value.starts_with("clang")) {
        result.family = CompilerFamily::Clang;
        result.invocation = "clang++";
        suffix = value.substr(5);
    } else if (value.starts_with("g++")) {
        result.family = CompilerFamily::Gcc;
        result.invocation = "g++";
        suffix = value.substr(3);
    } else if (value.starts_with("gcc")) {
        result.family = CompilerFamily::Gcc;
        result.invocation = "g++";
        suffix = value.substr(3);
    } else {
        std::size_t driver = value.rfind("-clang++");
        std::size_t length = 8;
        result.family = CompilerFamily::Clang;
        std::string normalized = "clang++";
        if (driver == std::string_view::npos) {
            driver = value.rfind("-clang");
            length = 6;
        }
        if (driver == std::string_view::npos) {
            driver = value.rfind("-g++");
            length = 4;
            result.family = CompilerFamily::Gcc;
            normalized = "g++";
        }
        if (driver == std::string_view::npos) {
            driver = value.rfind("-gcc");
            length = 4;
            result.family = CompilerFamily::Gcc;
            normalized = "g++";
        }
        if (driver == std::string_view::npos || driver == 0) return std::nullopt;
        result.target_prefix = std::string(value.substr(0, driver));
        if (!valid_target_triple(result.target_prefix)) return std::nullopt;
        result.invocation = result.target_prefix + "-" + normalized;
        suffix = value.substr(driver + length);
    }

    if (suffix.empty()) return result;
    if (!suffix.starts_with('-') || suffix.size() == 1) return std::nullopt;

    unsigned major = 0;
    const auto digits = suffix.substr(1);
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), major);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || major == 0)
        return std::nullopt;

    result.invocation += std::string(suffix);
    result.requested_major = major;
    return result;
}

bool valid_target_triple(std::string_view value) {
    if (value.empty() || value == "host" || value.front() == '-' || value.back() == '-')
        return false;
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) == 0 && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

std::optional<RunnerSettings> runner_profile(std::string_view profile,
                                             std::string_view target,
                                             const PlatformSettings* platform) {
    if (profile == "qemu-user" && target == "m68k-linux-gnu") {
        RunnerSettings runner;
        runner.invocation = "qemu-m68k";
        if (platform != nullptr && platform->runtime_prefix)
            runner.prefix_arguments = {"-L", platform->runtime_prefix->string()};
        return runner;
    }
    if (platform == nullptr) return std::nullopt;
    const auto* machine_entry = find_runner_machine(profile, target, platform->machine);
    if (machine_entry == nullptr) return std::nullopt;

    if (profile == "qemu-system" && target == "arm-none-eabi") {
        RunnerSettings runner;
        runner.invocation = machine_entry->invocation;
        runner.prefix_arguments = {"-M", std::string(machine_entry->machine), "-cpu", "cortex-m3",
                                   "-nographic", "-semihosting"};
        runner.image = RunnerImage::Option;
        runner.image_option = "-kernel";
        runner.image_arguments = {"-kernel", "{}"};
        runner.forwards_arguments = false;
        return runner;
    }
    if (profile == "openocd" && target == "arm-none-eabi") {
        RunnerSettings runner;
        runner.invocation = machine_entry->invocation;
        runner.prefix_arguments = {
            "-f", "interface/cmsis-dap.cfg",
            "-f", "target/" + std::string(machine_entry->machine) + ".cfg",
            "-c", "adapter speed 5000",
            "-c", "init",
            "-c", "arm semihosting enable",
        };
        runner.image = RunnerImage::Option;
        runner.image_option = "-c \"program ... verify reset\"";
        runner.image_arguments = {"-c", "program {} verify reset"};
        runner.forwards_arguments = false;
        return runner;
    }
    return std::nullopt;
}

std::optional<DebuggerSettings> debugger_profile(std::string_view profile,
                                                  std::string_view target,
                                                  const PlatformSettings* platform) {
    if (profile == "gdb" && target == "host")
        return DebuggerSettings{.invocation = "gdb"};
    if (profile == "gdb" && target == "m68k-linux-gnu") {
        return DebuggerSettings{
            .invocation = "gdb-multiarch",
            .prefix_arguments = {"-q"},
            .connection = DebuggerConnection::RunnerRemote,
            .remote_endpoint = "localhost:1234",
            .runner_arguments = {"-g", "1234"},
        };
    }
    if ((profile == "gdb" || profile == "openocd") && target == "arm-none-eabi" &&
        platform != nullptr) {
        const auto* machine_entry = find_runner_machine("openocd", target, platform->machine);
        if (machine_entry != nullptr) {
            return DebuggerSettings{
                .invocation = "gdb-multiarch",
                .prefix_arguments = {"-q"},
                .connection = DebuggerConnection::RunnerRemote,
                .remote_endpoint = "localhost:3333",
                .runner_arguments = {},
            };
        }
    }
    return std::nullopt;
}

std::string_view compiler_family_name(CompilerFamily family) {
    return family == CompilerFamily::Gcc ? "gcc" : "clang";
}

std::optional<Build> parse_build(std::string_view value) {
    if (value == "debug") return Build::Debug;
    if (value == "release") return Build::Release;
    return std::nullopt;
}

std::string_view build_name(Build build) {
    switch (build) {
        case Build::Debug: return "debug";
        case Build::Release: return "release";
    }
    return {};
}

std::filesystem::path host_output_directory() { return "out-host"; }

std::filesystem::path target_output_directory(std::string_view target) {
    if (target.empty() || target == "host") return host_output_directory();
    return std::filesystem::path("out-target-" + std::string(target));
}

BuildDefaults build_defaults(Build build) {
    return build == Build::Debug ? BuildDefaults{0, true, true} : BuildDefaults{2, false, false};
}

namespace {
std::string baseline_flags(Build build, bool link) {
    const auto defaults = build_defaults(build);
    std::string flags = "-std=c++20";
    if (!link || defaults.optimize != 0) flags += " -O" + std::to_string(defaults.optimize);
    if (defaults.debug_info) flags += " -g";
    if (!link && !defaults.assertions) flags += " -DNDEBUG";
    return flags;
}
}

std::string_view build_compile_flags(Build build) {
    static const auto debug = baseline_flags(Build::Debug, false);
    static const auto release = baseline_flags(Build::Release, false);
    return build == Build::Debug ? debug : release;
}

std::string_view build_link_flags(Build build) {
    static const auto debug = baseline_flags(Build::Debug, true);
    static const auto release = baseline_flags(Build::Release, true);
    return build == Build::Debug ? debug : release;
}

bool log_configuration(const ConfigurationLog& log) {
    std::error_code ec;
    const bool has_configuration = std::filesystem::exists(log.configuration_path, ec);
    if (ec) {
        std::cerr << log.tool << ": cannot check " << log.configuration_path.string() << ": "
                  << ec.message() << "\n";
        return false;
    }

    if (has_configuration)
        std::cout << "  configuration " << log.configuration_path.string() << "\n";
    else
        std::cout << "  configuration default\n";

    if (log.verbose) {
        std::cout << "    build         " << log.build << "\n";
        std::cout << "    family        " << log.compiler_family << "\n";
        std::cout << "    compiler      " << log.compiler << "\n";
        std::cout << "    compile flags " << log.compile_flags << "\n";
        std::cout << "    link flags    " << log.link_flags << "\n";
        std::cout << "    runner        " << (log.runner.empty() ? "none" : log.runner) << "\n";
        std::cout << "    debugger      "
                  << (log.debugger.empty() ? "none" : log.debugger) << "\n";
    }
    std::cout << "  target " << log.target.string() << "\n";
    return true;
}

bool write_configuration(const std::filesystem::path& project_root, const Settings& settings) {
    if (!valid_settings(settings)) return false;

    std::error_code ec;
    if (!std::filesystem::is_directory(project_root, ec) || ec) return false;

    std::ostringstream out;

    out << "---\n";
    out << "mm: " << (settings.configuration_2 ? "2.0" : "1.0") << '\n';
    if (settings.configuration_2) out << "schema: configuration-2\n";
    out << "kind: configuration\n";
    out << "name: " << settings.name << '\n';
    out << "build: " << build_name(settings.build) << '\n';
    out << "target-compiler: "
        << (settings.target_compiler == CompilerSelection::Host ? "host" : "cross") << '\n';
    out << "target-host-capability: "
        << (settings.target_has_host_capability ? "yes" : "no") << '\n';
    write_compiler(out, "host", settings.host);
    if (settings.host_debugger) write_debugger(out, "host", *settings.host_debugger);
    if (settings.cross) write_compiler(out, "cross", *settings.cross, !settings.configuration_2);
    if (settings.cross_debugger) write_debugger(out, "cross", *settings.cross_debugger);
    if (settings.cross_runner) write_runner(out, *settings.cross_runner);
    if (settings.configuration_2) write_platform(out, *settings.cross_platform);
    out << "host-build-directory: " << settings.host_build_directory.generic_string() << '\n';
    out << "target-build-directory: " << settings.target_build_directory.generic_string() << '\n';
    out << "---\n";
    // out/config.mdy names the lane, so it cannot live inside the lane it names:
    // a tool would have to know the answer to find the file that gives it.
    return write_atomic(project_root, "out", project_root / "out/config.mdy", out.str());
}

namespace {

enum class DefaultSource { Off, On, Optimization, DebugInfo, Assertions, Unset };
struct OptionSpec {
    std::string_view name;
    OptionType type;
    DefaultSource source;
    std::int64_t minimum = 0;
    std::int64_t maximum = 3;
};

// A fixed table, so std::array rather than a std::vector built at static
// initialization: the vector allocates before main, and passing its
// initializer_list by value draws a psabi note from the 32-bit ARM backend.
constexpr std::array<OptionSpec, 8> registry = {{
    {"warnings", OptionType::Boolean, DefaultSource::Off},
    {"warnings-error", OptionType::Boolean, DefaultSource::Off},
    {"optimize", OptionType::Number, DefaultSource::Optimization},
    {"debug-info", OptionType::Boolean, DefaultSource::DebugInfo},
    {"assertions", OptionType::Boolean, DefaultSource::Assertions},
    {"include-dir", OptionType::Directory, DefaultSource::Unset},
    // Capability, not tuning: which lanes a node can be built for. Both
    // default yes, so restriction is opt-in and today's tree is unchanged.
    {"buildable-host", OptionType::Boolean, DefaultSource::On},
    {"buildable-target", OptionType::Boolean, DefaultSource::On},
}};

const OptionSpec* option_spec(std::string_view name) {
    for (const auto& spec : registry)
        if (spec.name == name) return &spec;
    return nullptr;
}

OptionValues default_options(Build build) {
    const auto policy = build_defaults(build);
    OptionValues values;
    for (const auto& spec : registry) {
        OptionValue value;
        value.type = spec.type;
        switch (spec.source) {
            case DefaultSource::Off: break;
            case DefaultSource::On: value.boolean = true; break;
            case DefaultSource::Optimization: value.number = policy.optimize; break;
            case DefaultSource::DebugInfo: value.boolean = policy.debug_info; break;
            case DefaultSource::Assertions: value.boolean = policy.assertions; break;
            case DefaultSource::Unset: value.unset = true; break;
        }
        values.emplace(spec.name, std::move(value));
    }
    return values;
}

std::string_view trim_option(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
}

bool option_error(std::string_view tool, const OptionNode& node, std::string_view name,
                  std::string_view message) {
    std::cerr << tool << ": " << node.manifest.string() << ": " << name << ": "
              << message << "\n";
    return false;
}

bool relative_directory(const std::filesystem::path& root, const std::filesystem::path& path,
                        std::filesystem::path& result) {
    result = path.is_absolute() ? path.lexically_relative(root) : path;
    result = result.lexically_normal();
    if (result.empty() || result.is_absolute() || !valid_scalar(result.generic_string())) return false;
    for (const auto& part : result)
        if (part == "..") return false;
    return contained(root, root / result);
}

bool parse_value(const std::filesystem::path& root, const OptionNode& node,
                 const OptionSpec& spec, std::string_view text, OptionValue& value,
                 std::string_view tool) {
    const auto invalid = [&](std::string_view expected) {
        return option_error(tool, node, spec.name, "invalid value '" + std::string(text) +
                            "'; expected " + std::string(expected));
    };
    if (!valid_scalar(text)) return invalid("a non-empty single-line value");
    value.type = spec.type;
    value.unset = false;
    if (spec.type == OptionType::Boolean) {
        if (text != "yes" && text != "no") return invalid("yes or no");
        value.boolean = text == "yes";
    } else if (spec.type == OptionType::Number) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value.number);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            value.number < spec.minimum || value.number > spec.maximum)
            return invalid("a decimal integer from " + std::to_string(spec.minimum) +
                           " to " + std::to_string(spec.maximum) + " (no leading plus)");
    } else {
        const std::filesystem::path raw{text};
        std::filesystem::path declaring;
        std::filesystem::path relative;
        if (raw.is_absolute() || !relative_directory(root, node.directory, declaring) ||
            !relative_directory(root, declaring / raw, relative))
            return invalid("a directory relative to its manifest, inside the project");
        std::error_code ec;
        if (!std::filesystem::is_directory(root / relative, ec) || ec)
            return invalid("an existing directory inside the project");
        value.directory = relative.generic_string();
    }
    return true;
}

std::string option_text(const OptionValue& value) {
    if (value.unset) return "unset";
    if (value.type == OptionType::Boolean) return value.boolean ? "yes" : "no";
    if (value.type == OptionType::Number) return std::to_string(value.number);
    return value.directory;
}

void write_origins(std::ostream& out, Build build, const OptionValues& values) {
    for (const auto& [name, value] : values) {
        out << name << ": " << option_text(value) << "; ";
        if (value.origin == OptionOrigin::Default) out << "default for " << build_name(build);
        else if (value.origin == OptionOrigin::Assignment)
            out << "assigned by " << value.value_source.generic_string();
        else out << "reset by " << value.value_source.generic_string()
                 << " to default for " << build_name(build);
        if (value.read_only) out << "; read-only from " << value.lock_source.generic_string();
        else out << "; mutable";
        out << '\n';
    }
}

}  // namespace

bool resolve_options(const std::filesystem::path& project_root, Build build,
                     const std::vector<OptionNode>& nodes, std::vector<OptionValues>& resolved,
                     std::string_view tool) {
    resolved.clear();
    std::error_code ec;
    const auto root = std::filesystem::canonical(project_root, ec);
    if (ec || nodes.empty()) {
        std::cerr << tool << ": option resolution requires a project root and manifest tree\n";
        return false;
    }
    const auto defaults = default_options(build);
    std::vector<OptionValues> result;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if ((i == 0 && (node.parent != static_cast<std::size_t>(-1) || node.kind != "project")) ||
            (i != 0 && node.parent >= i))
            return option_error(tool, node, "tree",
                                "expected one project root and parent-before-child order");
        std::filesystem::path directory;
        if (!relative_directory(root, node.directory, directory) ||
            !valid_scalar(node.name) || !valid_scalar(node.manifest.generic_string()))
            return option_error(tool, node, "tree", "invalid node path or name");
        if (node.kind == "doc" && (!node.options.empty() || !node.resets.empty() || !node.read_only.empty()))
            return option_error(tool, node, "doc",
                                "option, reset, and read-only are not allowed on doc manifests");
        auto values = i == 0 ? defaults : result[node.parent];
        std::vector<std::string> assigned;
        for (const auto& operation : {std::string_view("option"), std::string_view("reset")}) {
            const auto& declarations = operation == "option" ? node.options : node.resets;
            for (const auto& declaration : declarations) {
                const auto text = trim_option(declaration);
                const auto split = text.find_first_of(" \t");
                const auto name = text.substr(0, split);
                // Syntax before meaning: a malformed declaration is reported as
                // one rather than as an unregistered name or a lock conflict.
                if (name.empty())
                    return option_error(tool, node, operation, "declaration requires a name");
                if (operation == "reset" && split != std::string_view::npos)
                    return option_error(tool, node, name, "reset takes a name only");
                const auto* spec = option_spec(name);
                if (spec == nullptr)
                    return option_error(tool, node, name, "unknown option name");
                for (const auto& seen : assigned)
                    if (seen == name)
                        return option_error(tool, node, name,
                                            "duplicate option/reset declaration");
                assigned.emplace_back(name);
                auto& value = values.find(name)->second;
                if (value.read_only)
                    return option_error(tool, node, name, std::string(operation) +
                                        " cannot change read-only option locked by " +
                                        value.lock_source.generic_string());
                if (operation == "reset") {
                    value = defaults.find(name)->second;
                    value.origin = OptionOrigin::Reset;
                } else {
                    const auto raw = split == std::string_view::npos ? std::string_view{} : trim_option(text.substr(split));
                    if (!parse_value(root, node, *spec, raw, value, tool)) return false;
                    value.origin = OptionOrigin::Assignment;
                }
                value.value_source = node.manifest.lexically_normal();
            }
        }
        std::vector<std::string> locked;
        for (const auto& declaration : node.read_only) {
            const auto text = trim_option(declaration);
            const auto split = text.find_first_of(" \t");
            const auto name = text.substr(0, split);
            if (name.empty())
                return option_error(tool, node, "read-only", "declaration requires a name");
            if (split != std::string_view::npos)
                return option_error(tool, node, name, "read-only takes a name only");
            if (option_spec(name) == nullptr)
                return option_error(tool, node, name, "read-only requires one registered name");
            for (const auto& seen : locked)
                if (seen == name)
                    return option_error(tool, node, name, "duplicate read-only declaration");
            locked.emplace_back(name);
            auto& value = values.find(name)->second;
            if (!value.read_only) {
                value.read_only = true;
                value.lock_source = node.manifest.lexically_normal();
            }
        }
        // A node buildable for no lane is a declaration error, not an empty
        // build: nothing downstream could act on it.
        if (!values.find("buildable-host")->second.boolean &&
            !values.find("buildable-target")->second.boolean)
            return option_error(tool, node, "buildable-host",
                                "a node must remain buildable for at least one lane");
        result.push_back(std::move(values));
    }
    resolved = std::move(result);
    return true;
}

bool write_option_records(const std::filesystem::path& project_root,
                          const std::filesystem::path& output_directory, Build build,
                          const std::vector<OptionNode>& nodes,
                          const std::vector<OptionValues>& resolved, bool verbose) {
    if (nodes.empty() || nodes.size() != resolved.size()) return false;
    if (!valid_build_directory(output_directory)) return false;
    const auto output = output_directory.lexically_normal();
    std::error_code ec;
    const auto root = std::filesystem::canonical(project_root, ec);
    if (ec) return false;
    std::vector<std::filesystem::path> destinations;
    // Validate every destination before publishing the first record.
    for (const auto& node : nodes) {
        std::filesystem::path directory;
        if (!relative_directory(root, node.directory, directory)) return false;
        const auto path = (root / output / directory / "resolved-options.mdy").lexically_normal();
        if (!safe_output(root, output, path)) {
            std::cerr << "configure: unsafe record destination: " << path.string() << '\n';
            return false;
        }
        destinations.push_back(path);
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        std::filesystem::path directory;
        if (!relative_directory(root, node.directory, directory)) return false;
        std::ostringstream out;
        out << "---\nmm: 1.0\nkind: resolved-options\nschema: configure-options-1\nname: "
            << node.name << "\nnode: " << directory.generic_string()
            << "\nmanifest: " << (directory / "mm.mdy").lexically_normal().generic_string()
            << "\nbuild: " << build_name(build)
            << "\noutput: " << output.generic_string()
            << "\nresolved-by: configure\napplied-by-build: capabilities\npath-base: project-root\n";
        for (const auto& [name, value] : resolved[i]) {
            if (value.unset) out << "unset-option: " << name << '\n';
            else out << "option: " << name << ' ' << option_text(value) << '\n';
            if (value.read_only) out << "read-only: " << name << '\n';
        }
        out << "---\n\n# Provenance\n\n";
        write_origins(out, build, resolved[i]);
        if (!write_atomic(root, output, destinations[i], out.str())) {
            std::cerr << "configure: cannot write " << destinations[i].string()
                      << "; incomplete snapshot, rerun configure\n";
            return false;
        }
        if (verbose) {
            std::cout << "  options " << directory.generic_string() << '\n';
            write_origins(std::cout, build, resolved[i]);
        }
    }
    std::cout << "Manifest options and locks recorded; build and test apply capabilities only\n";
    return true;
}

}  // namespace mm::configure
