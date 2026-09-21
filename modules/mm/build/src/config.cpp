// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/wait.h>

module mm.build;

import mm.configure;
import mm.json;
import mm.mdy;
import :detail;
import :config;
import :manifest;
import :compile;

// POSIX pipe declarations are hidden by newlib's strict C++ feature profile.
// Version and ABI probes run only in the host build tool, while the module's
// remaining interfaces stay compilable for target lanes.
extern "C" std::FILE* popen(const char*, const char*);
extern "C" int pclose(std::FILE*);

namespace mm::build {
bool kind_in(std::string_view kind, std::string_view kinds) {
    for (std::size_t begin = 0; begin < kinds.size();) {
        const auto end = kinds.find(' ', begin);
        const auto candidate = kinds.substr(begin, end == std::string_view::npos
                                                       ? kinds.size() - begin
                                                       : end - begin);
        if (kind == candidate) return true;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return false;
}
struct ManifestKeyRule {
    std::string_view key;
    int introduced;
    std::string_view kinds;
};

const std::vector<ManifestKeyRule> manifest_key_rules = {
    {"mm", 10, "project dir module app test doc sdk board library"},
    {"kind", 10, "project dir module app test doc sdk board library"},
    {"name", 10, "project dir module app test doc sdk board library"},
    {"folder", 10, "project dir library"},
    {"module", 10, "module"},
    {"file", 10, "module app doc board"},
    {"unit", 10, "test"},
    {"use", 10, "module app test"},
    {"option", 11, "project dir module app test"},
    {"reset", 11, "project dir module app test"},
    {"read-only", 11, "project dir module app test"},
    {"target", 12, "sdk"},
    {"compiler-family", 12, "sdk"},
    {"runtime", 12, "sdk"},
    {"specs-profile", 12, "sdk"},
    {"specs-file", 12, "sdk"},
    {"sysroot", 12, "sdk"},
    {"runtime-prefix", 12, "sdk"},
    {"provides", 12, "sdk board"},
    {"sdk", 12, "board"},
    {"cpu", 12, "board"},
    {"instruction-set", 12, "board"},
    {"float-abi", 12, "board"},
    {"security-domain", 12, "board"},
    {"machine", 12, "board"},
    {"linker-script", 12, "board"},
    {"requires-board", 12, "app test"},
    {"source", 12, "library"},
    {"licence", 12, "library"},
    {"include-directory", 12, "library"},
    {"library-directory", 12, "library"},
    {"link-archive", 12, "library"},
    {"link-input", 12, "library"},
    {"library", 12, "sdk module"},
    {"external-build", 12, "library"},
    {"platform-interface", 12, "module"},
    {"platform-provider", 12, "sdk board"},
    {"derives-from", 12, "board"},
    {"sketch", 13, "app"},
    {"sketch-library", 13, "app"},
    {"project", 13, "app dir"},
};

const ManifestKeyRule* manifest_key_rule(std::string_view key) {
    for (const auto& rule : manifest_key_rules)
        if (rule.key == key) return &rule;
    return nullptr;
}
// All source-manifest consumers share this gate. Configuration records have
// their own schema and do not pass through it.
bool valid_mm_version(const mm::mdy::MDYDocument& doc, const std::filesystem::path& manifest,
                      const LoadPolicy& policy) {
    const auto* versions = lookup(doc, "mm");
    if (doc.status != mm::mdy::ParseStatus::Ok || versions == nullptr ||
        versions->size() != 1 || manifest_version(versions->front()) == nullptr) {
        std::cerr << policy.tool << ": invalid or unsupported mm: version in "
                  << manifest.string() << " (supported: " << supported_manifest_versions()
                  << ")\n";
        return false;
    }
    const auto* version = manifest_version(versions->front());
    const auto kind = first(doc, "kind");
    for (const auto& [key, values] : doc.metadata) {
        const auto* rule = manifest_key_rule(key);
        if (rule == nullptr && version->rejects_unknown_keys) {
            std::cerr << policy.tool << ": " << manifest.string()
                      << ": unknown manifest key: " << key
                      << (policy.strict_tree ? "\n" : " (ignored)\n");
            if (policy.strict_tree) return false;
        }
        if (rule == nullptr) continue;
        if (version->number < rule->introduced) {
            std::cerr << policy.tool << ": " << manifest.string() << ": " << key
                      << " requires mm: " << manifest_version_name(rule->introduced) << "\n";
            return false;
        }
        if (!kind_in(kind, rule->kinds)) {
            std::cerr << policy.tool << ": " << manifest.string() << ": " << key
                      << " is not valid on kind: " << kind << "\n";
            return false;
        }
        const bool option = key == "option" || key == "reset" || key == "read-only";
        if (option && policy.warn_options) {
            for (const auto& value : values) {
                const auto name = value.substr(0, value.find_first_of(" \t"));
                if (structural_property_name(name)) continue;
                std::cerr << policy.tool << ": " << manifest.string() << ": " << key
                          << " " << name << " is ignored; continuing with existing build configuration\n";
            }
        }
    }
    return true;
}

bool configuration_scalar(const mm::mdy::MDYDocument& doc, std::string_view key,
                          const std::filesystem::path& path, std::string& value) {
    const auto* values = lookup(doc, key);
    if (values == nullptr || values->size() != 1 || values->front().empty()) {
        std::cerr << "build: configuration requires one non-empty " << key << ": "
                  << path.string() << "\n";
        return false;
    }
    value = values->front();
    return true;
}

bool configuration_build(const mm::mdy::MDYDocument& doc,
                         const std::filesystem::path& path, Build& build) {
    const auto* values = lookup(doc, "build");
    if (values == nullptr) {
        build = Build::Debug;
        return true;
    }
    if (values->size() != 1 || values->front().empty()) {
        std::cerr << "build: configuration requires one non-empty build: " << path.string()
                  << "\n";
        return false;
    }
    if (values->front() == "debug") {
        build = Build::Debug;
        return true;
    }
    if (values->front() == "release") {
        build = Build::Release;
        return true;
    }
    std::cerr << "build: configuration build must be debug or release: " << path.string()
              << "\n";
    return false;
}

bool configuration_boolean(const mm::mdy::MDYDocument& document, std::string_view key,
                           const std::filesystem::path& path, bool default_value,
                           bool& value) {
    const auto* values = lookup(document, key);
    if (values == nullptr) {
        value = default_value;
        return true;
    }
    if (values->size() != 1 || (values->front() != "yes" && values->front() != "no")) {
        std::cerr << "build: configuration " << key << " must be yes or no: "
                  << path.string() << "\n";
        return false;
    }
    value = values->front() == "yes";
    return true;
}

bool configuration_directory(const mm::mdy::MDYDocument& doc, std::string_view key,
                             const std::filesystem::path& path,
                             std::filesystem::path& directory) {
    std::string value;
    if (!configuration_scalar(doc, key, path, value)) return false;

    directory = value;
    const auto normalized = directory.lexically_normal();
    if (directory.is_absolute() || normalized.empty() || normalized == ".") {
        std::cerr << "build: configuration has unsafe " << key << ": " << value << "\n";
        return false;
    }
    for (const auto& component : normalized) {
        if (component != "..") continue;
        std::cerr << "build: configuration has unsafe " << key << ": " << value << "\n";
        return false;
    }

    directory = normalized;
    return true;
}

bool configuration_optional_scalar(const mm::mdy::MDYDocument& document,
                                   std::string_view key,
                                   const std::filesystem::path& path,
                                   std::string& value) {
    const auto* values = lookup(document, key);
    if (values == nullptr) {
        value.clear();
        return true;
    }
    if (values->size() != 1 || values->front().empty()) {
        std::cerr << "build: configuration requires at most one non-empty " << key << ": "
                  << path.string() << "\n";
        return false;
    }
    value = values->front();
    return true;
}

bool configuration_compiler(const mm::mdy::MDYDocument& document, std::string_view prefix,
                            const std::filesystem::path& path, Toolchain& toolchain,
                            bool read_platform = true) {
    std::string platform;
    const auto family_key = std::string(prefix) + "-compiler-family";
    const auto* family_values = lookup(document, family_key);
    if (family_values != nullptr) {
        if (family_values->size() != 1 || family_values->front().empty()) {
            std::cerr << "build: configuration requires one non-empty " << family_key << ": "
                      << path.string() << "\n";
            return false;
        }
        if (family_values->front() == "gcc")
            toolchain.family = CompilerFamily::Gcc;
        else if (family_values->front() == "clang")
            toolchain.family = CompilerFamily::Clang;
        else {
            std::cerr << "build: configuration " << family_key << " must be gcc or clang: "
                      << path.string() << "\n";
            return false;
        }
    }

    if (!configuration_scalar(document, std::string(prefix) + "-compiler", path,
                              toolchain.compiler.invocation) ||
        !configuration_scalar(document, std::string(prefix) + "-target", path,
                              toolchain.target) ||
        !configuration_scalar(document, std::string(prefix) + "-compile-flags", path,
                              toolchain.compiler.arguments) ||
        !configuration_scalar(document, std::string(prefix) + "-link-flags", path,
                              toolchain.linker.arguments))
        return false;

    if (read_platform &&
        !configuration_scalar(document, std::string(prefix) + "-platform", path, platform))
        return false;
    if (read_platform && platform != "POSIX") {
        std::cerr << "build: configuration names unsupported " << prefix
                  << " platform: " << platform << "\n";
        return false;
    }
    toolchain.assembler.invocation = toolchain.compiler.invocation;
    toolchain.linker.invocation = toolchain.compiler.invocation;
    toolchain.librarian = {};
    toolchain.debugger.reset();
    std::string c_compiler;
    if (!configuration_optional_scalar(document, std::string(prefix) + "-c-compiler", path,
                                       c_compiler))
        return false;
    toolchain.c_compiler.invocation = c_compiler;
    return true;
}

bool configuration_2_key(std::string_view key) {
    static const std::set<std::string, std::less<>> keys = {
        "mm", "schema", "kind", "name", "build", "target-compiler",
        "target-host-capability", "host-compiler-family", "host-compiler", "host-c-compiler", "host-target",
        "host-platform", "host-compile-flags", "host-link-flags", "host-debugger",
        "host-debugger-prefix-argument", "host-debugger-connection",
        "host-debugger-remote-endpoint", "host-debugger-runner-argument",
        "cross-compiler-family", "cross-compiler", "cross-c-compiler", "cross-target", "cross-compile-flags",
        "cross-link-flags", "cross-debugger", "cross-debugger-prefix-argument",
        "cross-debugger-connection", "cross-debugger-remote-endpoint",
        "cross-debugger-runner-argument", "cross-runner", "cross-runner-prefix-argument",
        "cross-runner-image", "cross-runner-image-option", "cross-runner-image-argument",
        "cross-runner-suffix-argument",
        "cross-runner-forwards-arguments", "host-build-directory", "target-build-directory",
        "cross-system", "cross-runtime", "cross-sdk", "cross-sdk-manifest",
        "cross-sdk-compiler-family", "cross-sdk-sysroot", "cross-sdk-runtime-prefix",
        "cross-sdk-specs-argument", "cross-sdk-provides", "cross-link", "cross-board",
        "cross-board-manifest", "cross-board-derives-from", "cross-board-machine", "cross-board-linker-script",
        "cross-board-source", "cross-board-provides", "cross-board-argument",
        "cross-unresolved"};
    return keys.contains(key);
}

std::optional<mm::configure::PlatformSystem> parse_system(std::string_view value) {
    using S = mm::configure::PlatformSystem;
    if (value == "linux") return S::Linux;
    if (value == "bare-metal") return S::BareMetal;
    if (value == "unknown") return S::Unknown;
    return std::nullopt;
}

std::optional<mm::configure::PlatformRuntime> parse_runtime(std::string_view value) {
    using R = mm::configure::PlatformRuntime;
    if (value == "unknown") return R::Unknown;
    if (value == "glibc") return R::Glibc;
    if (value == "newlib") return R::Newlib;
    if (value == "picolibc") return R::Picolibc;
    if (value == "none") return R::None;
    return std::nullopt;
}

bool configuration_project_file(const std::filesystem::path& configuration,
                                std::string_view key, std::string_view raw,
                                std::filesystem::path& result) {
    const std::filesystem::path relative(raw);
    const auto normalized = relative.lexically_normal();
    if (relative.is_absolute() || normalized.empty() || normalized == ".") {
        std::cerr << "build: unsafe " << key << " in " << configuration.string() << "\n";
        return false;
    }
    for (const auto& component : normalized) {
        if (component != "..") continue;
        std::cerr << "build: unsafe " << key << " in " << configuration.string() << "\n";
        return false;
    }

    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(
        configuration.parent_path().parent_path(), ec);
    if (ec) return false;
    const auto file = std::filesystem::weakly_canonical(root / normalized, ec);
    const auto inside = file.lexically_relative(root);
    if (ec || inside.empty() || *inside.begin() == ".." ||
        !std::filesystem::is_regular_file(file, ec) || ec) {
        std::cerr << "build: " << key << " is not a project file: " << raw << "\n";
        return false;
    }
    result = normalized;
    return true;
}

bool configuration_external_directory(const std::filesystem::path& configuration,
                                      std::string_view key, std::string_view raw,
                                      std::optional<std::filesystem::path>& result) {
    if (raw.empty()) {
        result.reset();
        return true;
    }
    const std::filesystem::path directory(raw);
    std::error_code ec;
    if (!directory.is_absolute() || !std::filesystem::is_directory(directory, ec) || ec) {
        std::cerr << "build: " << key << " is not an existing absolute directory in "
                  << configuration.string() << "\n";
        return false;
    }
    result = directory;
    return true;
}

bool load_platform(const mm::mdy::MDYDocument& document,
                   const std::filesystem::path& path,
                   const Toolchain& cross,
                   Platform& platform) {
    std::string system;
    std::string runtime;
    std::string sdk;
    std::string sdk_manifest;
    std::string sdk_family;
    if (!configuration_scalar(document, "cross-system", path, system) ||
        !configuration_scalar(document, "cross-runtime", path, runtime) ||
        !configuration_scalar(document, "cross-sdk", path, sdk) ||
        !configuration_scalar(document, "cross-sdk-manifest", path, sdk_manifest) ||
        !configuration_scalar(document, "cross-sdk-compiler-family", path, sdk_family))
        return false;
    const auto parsed_system = parse_system(system);
    const auto parsed_runtime = parse_runtime(runtime);
    if (!parsed_system || !parsed_runtime || (sdk_family != "gcc" && sdk_family != "clang")) {
        std::cerr << "build: invalid target platform in " << path.string() << "\n";
        return false;
    }
    platform = {};
    platform.target = cross.target;
    platform.system = *parsed_system;
    platform.runtime = *parsed_runtime;
    const auto derived_system = mm::configure::target_system(cross.target);
    if (!derived_system || *derived_system != *parsed_system) {
        std::cerr << "build: target platform system disagrees with target " << cross.target
                  << "\n";
        return false;
    }
    platform.sdk = sdk;
    std::filesystem::path sdk_manifest_path;
    if (!configuration_project_file(path, "cross-sdk-manifest", sdk_manifest,
                                    sdk_manifest_path))
        return false;
    platform.sdk_manifest = std::move(sdk_manifest_path);
    platform.sdk_family = sdk_family == "gcc" ? CompilerFamily::Gcc : CompilerFamily::Clang;
    if (platform.sdk_family != cross.family) {
        std::cerr << "build: target platform compiler family disagrees with toolchain\n";
        return false;
    }
    std::string value;
    if (!configuration_optional_scalar(document, "cross-sdk-sysroot", path, value) ||
        !configuration_external_directory(path, "cross-sdk-sysroot", value,
                                          platform.sysroot))
        return false;
    if (!configuration_optional_scalar(document, "cross-sdk-runtime-prefix", path, value) ||
        !configuration_external_directory(path, "cross-sdk-runtime-prefix", value,
                                          platform.runtime_prefix))
        return false;
    if (!configuration_optional_scalar(document, "cross-sdk-specs-argument", path, platform.specs_argument))
        return false;
    std::string cross_link;
    if (!configuration_optional_scalar(document, "cross-link", path, cross_link))
        return false;
    if (!cross_link.empty()) {
        if (cross_link != "external") {
            std::cerr << "build: invalid cross-link in " << path.string() << "\n";
            return false;
        }
        platform.link_ownership = mm::configure::LinkOwnership::External;
    }
    if (!configuration_optional_scalar(document, "cross-board", path, value)) return false;
    if (!value.empty()) platform.board = value;
    if (!configuration_optional_scalar(document, "cross-board-manifest", path, value)) return false;
    if (!value.empty()) {
        std::filesystem::path board_manifest;
        if (!configuration_project_file(path, "cross-board-manifest", value, board_manifest))
            return false;
        platform.board_manifest = std::move(board_manifest);
    }
    if (platform.board.has_value() != platform.board_manifest.has_value()) {
        std::cerr << "build: incomplete board platform in " << path.string() << "\n";
        return false;
    }
    if (!configuration_optional_scalar(document, "cross-board-machine", path, platform.machine) ||
        !configuration_optional_scalar(document, "cross-board-linker-script", path, value))
        return false;
    if (!value.empty() &&
        !configuration_project_file(path, "cross-board-linker-script", value,
                                    platform.linker_script))
        return false;
    for (const auto& source : all(document, "cross-board-source")) {
        std::filesystem::path file;
        if (!configuration_project_file(path, "cross-board-source", source, file)) return false;
        platform.board_sources.push_back(std::move(file));
    }
    for (const auto& base : all(document, "cross-board-derives-from")) {
        if (base.empty() || !is_safe_board_name(base)) {
            std::cerr << "build: invalid cross-board-derives-from in " << path.string() << "\n";
            return false;
        }
        platform.board_derives_from.push_back(base);
    }
    platform.compiler_arguments = all(document, "cross-board-argument");

    const bool has_board_details = !platform.machine.empty() || !platform.linker_script.empty() ||
                                   !platform.board_sources.empty() ||
                                   !platform.compiler_arguments.empty() ||
                                   lookup(document, "cross-board-provides") != nullptr ||
                                   !platform.board_derives_from.empty();
    if (!platform.board && has_board_details) {
        std::cerr << "build: board details require cross-board in " << path.string() << "\n";
        return false;
    }
    if (platform.link_ownership == mm::configure::LinkOwnership::External &&
        !platform.linker_script.empty()) {
        std::cerr << "build: external-link platform cannot declare linker script: "
                  << platform.linker_script.string() << " in " << path.string() << "\n";
        return false;
    }
    if (platform.board) {
        const auto* entry = find_processor_entry_by_arguments(
            platform.sdk_family, platform.target, platform.compiler_arguments);
        if (*parsed_system == mm::configure::PlatformSystem::Linux) {
            if (!platform.linker_script.empty()) {
                std::cerr << "build: hosted board cannot declare linker script in "
                          << path.string() << "\n";
                return false;
            }
            if (entry == nullptr) {
                std::cerr << "build: unknown processor combination for hosted board "
                          << *platform.board << " in " << path.string() << "\n";
                return false;
            }
        } else if (platform.link_ownership == mm::configure::LinkOwnership::External) {
            if (*parsed_system != mm::configure::PlatformSystem::BareMetal) {
                std::cerr << "build: board platform requires bare-metal system in "
                          << path.string() << "\n";
                return false;
            }
            if (platform.machine.empty()) {
                std::cerr << "build: board platform requires machine in " << path.string() << "\n";
                return false;
            }
            if (entry == nullptr) {
                std::cerr << "build: unknown processor combination for board " << *platform.board
                          << " in " << path.string() << "\n";
                return false;
            }
        } else {
            if (*parsed_system != mm::configure::PlatformSystem::BareMetal) {
                std::cerr << "build: board platform requires bare-metal system in "
                          << path.string() << "\n";
                return false;
            }
            if (platform.linker_script.empty()) {
                std::cerr << "build: board platform requires linker script in "
                          << path.string() << "\n";
                return false;
            }
            if (platform.board_sources.empty()) {
                std::cerr << "build: board platform requires at least one source in "
                          << path.string() << "\n";
                return false;
            }
            if (entry == nullptr) {
                std::cerr << "build: unknown processor combination for board " << *platform.board
                          << " in " << path.string() << "\n";
                return false;
            }
        }
        if (cross.runner && cross.runner->image == RunnerImage::Option) {
            if (platform.machine.empty()) {
                std::cerr << "build: target system runner requires board machine\n";
                return false;
            }
            const auto* machine_entry = mm::configure::find_runner_machine_by_invocation(
                cross.runner->invocation, platform.target, platform.machine);
            if (machine_entry == nullptr) {
                std::cerr << "build: " << cross.runner->invocation
                          << " runner requires matching board machine for target "
                          << platform.target << "\n";
                return false;
            }
        }
    }

    std::set<mm::configure::Responsibility> seen;
    auto add_owned = [&](std::string_view key, const std::string& owner) {
        for (const auto& text : all(document, key)) {
            const auto responsibility = parse_responsibility(text);
            if (!responsibility || !seen.insert(*responsibility).second) {
                std::cerr << "build: invalid or duplicate " << key << ": " << text << "\n";
                return false;
            }
            platform.responsibility_owners[*responsibility] = owner;
        }
        return true;
    };
    if (!add_owned("cross-sdk-provides", sdk)) return false;
    if (platform.board && !add_owned("cross-board-provides", *platform.board)) return false;
    for (const auto& text : all(document, "cross-unresolved")) {
        const auto responsibility = parse_responsibility(text);
        if (!responsibility || !seen.insert(*responsibility).second) {
            std::cerr << "build: invalid or duplicate cross-unresolved: " << text << "\n";
            return false;
        }
        platform.unresolved.push_back(*responsibility);
    }
    platform.models_responsibilities = platform.system == mm::configure::PlatformSystem::BareMetal;
    if (!platform.models_responsibilities && !seen.empty()) {
        std::cerr << "build: hosted platform cannot carry responsibility state in "
                  << path.string() << "\n";
        return false;
    }
    if (platform.models_responsibilities && seen.size() != 5) {
        std::cerr << "build: incomplete responsibility state in " << path.string() << "\n";
        return false;
    }
    if (platform.models_responsibilities && platform.board && !platform.unresolved.empty()) {
        std::cerr << "build: configured board cannot leave responsibilities unresolved in "
                  << path.string() << "\n";
        return false;
    }
    if (platform.models_responsibilities && !platform.board) {
        const std::vector<mm::configure::Responsibility> expected = {
            mm::configure::Responsibility::ResetVector,
            mm::configure::Responsibility::InitialStack,
            mm::configure::Responsibility::MemoryLayout};
        if (platform.unresolved != expected) {
            std::cerr << "build: invalid boardless responsibility state in "
                      << path.string() << "\n";
            return false;
        }
    }
    return true;
}

bool has_configuration_compiler(const mm::mdy::MDYDocument& document,
                                std::string_view prefix) {
    return lookup(document, std::string(prefix) + "-compiler-family") != nullptr ||
           lookup(document, std::string(prefix) + "-compiler") != nullptr ||
           lookup(document, std::string(prefix) + "-c-compiler") != nullptr ||
           lookup(document, std::string(prefix) + "-target") != nullptr ||
           lookup(document, std::string(prefix) + "-platform") != nullptr ||
           lookup(document, std::string(prefix) + "-compile-flags") != nullptr ||
           lookup(document, std::string(prefix) + "-link-flags") != nullptr;
}

bool configuration_runner(const mm::mdy::MDYDocument& document,
                          const std::filesystem::path& path,
                          std::optional<ToolchainRunner>& result) {
    const auto* invocation = lookup(document, "cross-runner");
    const bool has_any = invocation != nullptr ||
        lookup(document, "cross-runner-prefix-argument") != nullptr ||
        lookup(document, "cross-runner-image") != nullptr ||
        lookup(document, "cross-runner-image-option") != nullptr ||
        lookup(document, "cross-runner-image-argument") != nullptr ||
        lookup(document, "cross-runner-suffix-argument") != nullptr ||
        lookup(document, "cross-runner-forwards-arguments") != nullptr;
    if (!has_any) {
        result.reset();
        return true;
    }
    if (invocation == nullptr || invocation->size() != 1 || invocation->front().empty()) {
        std::cerr << "build: configuration requires one non-empty cross-runner: "
                  << path.string() << "\n";
        return false;
    }

    ToolchainRunner runner;
    runner.invocation = invocation->front();
    if (const auto* values = lookup(document, "cross-runner-prefix-argument"))
        runner.prefix_arguments = *values;
    if (const auto* values = lookup(document, "cross-runner-suffix-argument"))
        runner.suffix_arguments = *values;

    std::string image;
    if (!configuration_scalar(document, "cross-runner-image", path, image)) return false;
    if (image == "positional") {
        runner.image = RunnerImage::Positional;
        if (lookup(document, "cross-runner-image-option") != nullptr ||
            lookup(document, "cross-runner-image-argument") != nullptr) {
            std::cerr << "build: positional runner cannot have cross-runner-image-option or cross-runner-image-argument: "
                      << path.string() << "\n";
            return false;
        }
    } else if (image == "option") {
        runner.image = RunnerImage::Option;
        if (const auto* values = lookup(document, "cross-runner-image-argument"))
            runner.image_arguments = *values;
        if (lookup(document, "cross-runner-image-option") != nullptr) {
            if (!configuration_scalar(document, "cross-runner-image-option", path,
                                      runner.image_option))
                return false;
        }
        if (runner.image_arguments.empty() && !runner.image_option.empty())
            runner.image_arguments = {runner.image_option, "{}"};
        if (runner.image_arguments.empty() && runner.image_option.empty()) {
            std::cerr << "build: option runner requires cross-runner-image-option or cross-runner-image-argument: "
                      << path.string() << "\n";
            return false;
        }
    } else {
        std::cerr << "build: configuration cross-runner-image must be positional or option: "
                  << path.string() << "\n";
        return false;
    }
    if (!configuration_boolean(document, "cross-runner-forwards-arguments", path, true,
                               runner.forwards_arguments))
        return false;
    result = std::move(runner);
    return true;
}

bool configuration_debugger(const mm::mdy::MDYDocument& document,
                            std::string_view prefix,
                            const std::filesystem::path& path,
                            std::optional<ToolchainDebugger>& result) {
    const std::string base = std::string(prefix) + "-debugger";
    const auto* invocation = lookup(document, base);
    const bool has_any = invocation != nullptr ||
        lookup(document, base + "-prefix-argument") != nullptr ||
        lookup(document, base + "-connection") != nullptr ||
        lookup(document, base + "-remote-endpoint") != nullptr ||
        lookup(document, base + "-runner-argument") != nullptr;
    if (!has_any) {
        result.reset();
        return true;
    }
    if (invocation == nullptr || invocation->size() != 1 || invocation->front().empty()) {
        std::cerr << "build: configuration requires one non-empty " << base << ": "
                  << path.string() << "\n";
        return false;
    }

    ToolchainDebugger debugger;
    debugger.invocation = invocation->front();
    if (const auto* values = lookup(document, base + "-prefix-argument"))
        debugger.prefix_arguments = *values;
    if (const auto* values = lookup(document, base + "-runner-argument"))
        debugger.runner_arguments = *values;

    std::string connection;
    if (!configuration_scalar(document, base + "-connection", path, connection)) return false;
    if (connection == "direct") {
        debugger.connection = DebuggerConnection::Direct;
        if (lookup(document, base + "-remote-endpoint") != nullptr ||
            !debugger.runner_arguments.empty()) {
            std::cerr << "build: direct " << base
                      << " cannot have remote or runner arguments: " << path.string() << "\n";
            return false;
        }
    } else if (connection == "runner-remote") {
        debugger.connection = DebuggerConnection::RunnerRemote;
        if (!configuration_scalar(document, base + "-remote-endpoint", path,
                                  debugger.remote_endpoint))
            return false;
    } else {
        std::cerr << "build: configuration " << base
                  << "-connection must be direct or runner-remote: " << path.string() << "\n";
        return false;
    }
    result = std::move(debugger);
    return true;
}
Toolchain default_toolchain(bool verbose) {
    Toolchain toolchain;
    toolchain.verbose = verbose;
    return toolchain;
}

bool validate_manifest_schema(const mm::mdy::MDYDocument& document,
                              const std::filesystem::path& manifest, const LoadPolicy& policy) {
    return valid_manifest(document, first(document, "kind"), first(document, "name"), manifest, policy);
}

bool load_configuration(const std::filesystem::path& path, bool verbose,
                        BuildConfiguration& configuration) {
    const auto document = mm::mdy::Parser::parse_file(path);
    if (document.status != mm::mdy::ParseStatus::Ok) {
        std::cerr << "build: cannot read configuration: " << path.string() << "\n";
        return false;
    }

    std::string version;
    std::string kind;
    std::string name;
    std::string selection;
    if (!configuration_scalar(document, "mm", path, version) ||
        (version != "1.0" && version != "2.0") ||
        !configuration_scalar(document, "kind", path, kind) || kind != "configuration" ||
        !configuration_scalar(document, "name", path, name) ||
        !configuration_scalar(document, "target-compiler", path, selection)) {
        std::cerr << "build: invalid configuration: " << path.string() << "\n";
        return false;
    }
    const bool configuration_2 = version == "2.0";
    std::string schema;
    if (configuration_2) {
        if (!configuration_scalar(document, "schema", path, schema) ||
            schema != "configuration-2") {
            std::cerr << "build: mm 2.0 configuration requires schema configuration-2: "
                      << path.string() << "\n";
            return false;
        }
        for (const auto& [key, values] : document.metadata) {
            if (!configuration_2_key(key)) {
                std::cerr << "build: unknown configuration-2 key: " << key << "\n";
                return false;
            }
            const bool repeated = key == "cross-sdk-provides" ||
                                  key == "cross-board-source" ||
                                  key == "cross-board-derives-from" ||
                                  key == "cross-board-provides" ||
                                  key == "cross-board-argument" ||
                                  key == "cross-unresolved" ||
                                  key.ends_with("-prefix-argument") ||
                                  key.ends_with("-suffix-argument") ||
                                  key.ends_with("-runner-argument") ||
                                  key.ends_with("-image-argument");
            if (!repeated && values.size() != 1) {
                std::cerr << "build: duplicated configuration-2 key: " << key << "\n";
                return false;
            }
        }
        if (lookup(document, "cross-platform") != nullptr) {
            std::cerr << "build: cross-platform is not valid in configuration-2\n";
            return false;
        }
    } else if (lookup(document, "schema") != nullptr) {
        std::cerr << "build: mm 1.0 configuration cannot carry schema\n";
        return false;
    }

    if (selection != "host" && selection != "cross") {
        std::cerr << "build: configuration target-compiler must be host or cross: "
                  << path.string() << "\n";
        return false;
    }

    Build build;
    if (!configuration_build(document, path, build)) return false;

    Toolchain host;
    if (!configuration_compiler(document, "host", path, host)) return false;
    if (!configuration_debugger(document, "host", path, host.debugger)) return false;
    if (host.debugger && host.debugger->connection != DebuggerConnection::Direct) {
        std::cerr << "build: host debugger must use a direct connection: " << path.string()
                  << "\n";
        return false;
    }

    Toolchain cross;
    const bool has_cross = has_configuration_compiler(document, "cross");
    if (configuration_2 && !has_cross) {
        std::cerr << "build: configuration-2 requires a target toolchain: "
                  << path.string() << "\n";
        return false;
    }
    if (has_cross && !configuration_compiler(document, "cross", path, cross, !configuration_2))
        return false;
    if (!configuration_debugger(document, "cross", path, cross.debugger)) return false;
    if (cross.debugger && !has_cross) {
        std::cerr << "build: configuration gives a debugger to a missing target: "
                  << path.string() << "\n";
        return false;
    }
    if (cross.debugger && cross.debugger->connection != DebuggerConnection::RunnerRemote) {
        std::cerr << "build: target debugger must use a runner-remote connection: "
                  << path.string() << "\n";
        return false;
    }

    std::optional<ToolchainRunner> cross_runner;
    if (!configuration_runner(document, path, cross_runner)) return false;
    if (cross_runner && !has_cross) {
        std::cerr << "build: configuration gives a runner to a missing target: "
                  << path.string() << "\n";
        return false;
    }
    if (cross_runner) cross.runner = std::move(cross_runner);
    if (cross.debugger && cross.debugger->connection == DebuggerConnection::RunnerRemote &&
        !cross.runner) {
        std::cerr << "build: remote target debugger requires a runner: " << path.string()
                  << "\n";
        return false;
    }

    if (selection == "cross") {
        if (!has_cross) {
            std::cerr << "build: configuration selects cross without a cross compiler: "
                      << path.string() << "\n";
            return false;
        }
    }

    Platform host_platform;
    host_platform.target = "host";
    host_platform.system = mm::configure::PlatformSystem::Posix;
    host_platform.runtime = mm::configure::PlatformRuntime::Unknown;
    std::optional<Platform> cross_platform;
    if (has_cross) {
        Platform value;
        if (configuration_2) {
            if (!load_platform(document, path, cross, value)) return false;
            for (const auto& argument : value.compiler_arguments) {
                cross.compiler.arguments += " " + argument;
                cross.linker.arguments += " " + argument;
            }
            if (value.sysroot) {
                cross.compiler.arguments += " --sysroot " + shell_quote(*value.sysroot);
                cross.linker.arguments += " --sysroot " + shell_quote(*value.sysroot);
            }
            if (!value.specs_argument.empty())
                cross.linker.arguments += " " + value.specs_argument;
            if (!value.linker_script.empty())
                cross.linker.arguments += " -T " + shell_quote(value.linker_script);
        } else {
            value.target = cross.target;
            value.system = mm::configure::target_system(cross.target).value_or(
                mm::configure::PlatformSystem::Unknown);
            value.runtime = mm::configure::PlatformRuntime::Unknown;
        }
        cross_platform = std::move(value);
    }
    bool target_has_host_capability = false;
    if (!configuration_boolean(document, "target-host-capability", path, false,
                               target_has_host_capability))
        return false;
    if (target_has_host_capability && !has_cross) {
        std::cerr << "build: configuration gives host capability to a missing target: "
                  << path.string() << "\n";
        return false;
    }

    std::filesystem::path host_directory;
    std::filesystem::path target_directory;
    if (!configuration_directory(document, "host-build-directory", path, host_directory) ||
        !configuration_directory(document, "target-build-directory", path, target_directory))
        return false;

    host.verbose = verbose;
    if (has_cross) cross.verbose = verbose;
    configuration.host_ = std::move(host);
    configuration.cross_ = has_cross ? std::optional<Toolchain>(std::move(cross)) : std::nullopt;
    configuration.cross_build_directory_ =
        has_cross ? std::optional<std::filesystem::path>(target_directory) : std::nullopt;
    configuration.selects_cross_ = selection == "cross";
    configuration.target_has_host_capability_ = target_has_host_capability;
    configuration.host_platform_ = std::move(host_platform);
    configuration.cross_platform_ = std::move(cross_platform);
    configuration.configuration_2_ = configuration_2;
    configuration.host_build_directory = host_directory;
    configuration.build = build;
    configuration.build_directory = selection == "cross" ? std::move(target_directory)
                                                           : std::move(host_directory);
    return true;
}

bool resolve_configuration(const std::filesystem::path& project_root, bool verbose,
                           BuildConfiguration& configuration) {
    const auto path = project_root / "out" / "config.mdy";
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        std::cerr << "build: cannot check " << path.string() << ": " << ec.message() << "\n";
        return false;
    }

    if (exists) return load_configuration(path, verbose, configuration);

    configuration.host_ = default_toolchain(verbose);
    configuration.cross_.reset();
    configuration.cross_build_directory_.reset();
    configuration.host_platform_ = {};
    configuration.host_platform_.target = "host";
    configuration.host_platform_.system = mm::configure::PlatformSystem::Posix;
    configuration.cross_platform_.reset();
    configuration.selects_cross_ = false;
    configuration.target_has_host_capability_ = false;
    configuration.configuration_2_ = false;
    configuration.build = Build::Debug;
    configuration.build_directory = "out";
    configuration.host_build_directory = "out";
    return true;
}
}
