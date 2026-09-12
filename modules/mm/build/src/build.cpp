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

import mm.mdy;

namespace mm::build {

namespace {

std::optional<mm::configure::Responsibility> parse_responsibility(std::string_view value);

const std::vector<std::string>* lookup(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto it = doc.metadata.find(key);
    return it == doc.metadata.end() ? nullptr : &it->second;
}

std::string first(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto* values = lookup(doc, key);
    return values == nullptr || values->empty() ? std::string{} : values->front();
}

std::vector<std::string> all(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto* values = lookup(doc, key);
    return values == nullptr ? std::vector<std::string>{} : *values;
}

std::string_view option_name(std::string_view declaration) {
    return declaration.substr(0, declaration.find_first_of(" \t"));
}

bool structural_property_name(std::string_view name) {
    return name == "buildable-host" || name == "buildable-target" || name == "core";
}

struct ManifestVersionRule {
    std::string_view name;
    int number;
    bool rejects_unknown_keys;
};

constexpr ManifestVersionRule manifest_versions[] = {
    {"1.0", 10, false},
    {"1.1", 11, true},
    {"1.2", 12, true},
    {"1.3", 13, true},
    {"1.4", 14, true},
};

const ManifestVersionRule* manifest_version(std::string_view version) {
    for (const auto& candidate : manifest_versions)
        if (candidate.name == version) return &candidate;
    return nullptr;
}

std::string supported_manifest_versions() {
    std::string result;
    for (const auto& version : manifest_versions) {
        if (!result.empty()) result += ", ";
        result += version.name;
    }
    return result;
}

std::string_view manifest_version_name(int number) {
    for (const auto& version : manifest_versions)
        if (version.number == number) return version.name;
    return "unknown";
}

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
    {"machine", 12, "board"},
    {"linker-script", 12, "board"},
    {"requires-board", 12, "app test"},
    {"source", 13, "library"},
    {"licence", 13, "library"},
    {"include-directory", 13, "library"},
    {"library-directory", 13, "library"},
    {"link-archive", 13, "library"},
    {"link-input", 13, "library"},
    {"library", 13, "sdk module"},
    {"external-build", 14, "library"},
};

const ManifestKeyRule* manifest_key_rule(std::string_view key) {
    for (const auto& rule : manifest_key_rules)
        if (rule.key == key) return &rule;
    return nullptr;
}

struct ProcessorEntry {
    CompilerFamily family;
    std::string_view target;
    std::string_view cpu;
    std::string_view instruction_set;
    std::string_view float_abi;
    std::string_view arguments[3];
};

constexpr ProcessorEntry processor_table[] = {
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m3", "thumb", "soft",
     {"-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft"}},
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m0plus", "thumb", "soft",
     {"-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft"}},
    {CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33", "thumb", "softfp",
     {"-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=softfp"}},
};

const ProcessorEntry* find_processor_entry(
    CompilerFamily family,
    std::string_view target,
    std::string_view cpu,
    std::string_view instruction_set,
    std::string_view float_abi) {
    for (const auto& entry : processor_table) {
        if (entry.family == family && entry.target == target &&
            entry.cpu == cpu && entry.instruction_set == instruction_set &&
            entry.float_abi == float_abi) {
            return &entry;
        }
    }
    return nullptr;
}

const ProcessorEntry* find_processor_entry_by_arguments(
    CompilerFamily family,
    std::string_view target,
    const std::vector<std::string>& arguments) {
    if (arguments.size() != 3) return nullptr;
    for (const auto& entry : processor_table) {
        if (entry.family == family && entry.target == target &&
            arguments[0] == entry.arguments[0] &&
            arguments[1] == entry.arguments[1] &&
            arguments[2] == entry.arguments[2]) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<std::string> structural_property_declarations(
    const std::vector<std::string>& declarations) {
    std::vector<std::string> result;
    for (const auto& declaration : declarations)
        if (structural_property_name(option_name(declaration))) result.push_back(declaration);
    return result;
}

bool safe_exists(const std::filesystem::path& path) {
    std::error_code ec;
    const bool found = std::filesystem::exists(path, ec);
    if (ec) std::cerr << "build: cannot check " << path.string() << ": " << ec.message() << "\n";
    return found;
}

// A folder: entry can name any path, including "." or one that climbs out of the
// project or loops back through a symlink. Manifests are therefore identified by
// their canonical path, and the walk keeps two sets: the chain currently being
// visited, which detects cycles, and everything finished, which collapses a
// diamond into one visit instead of duplicating its targets.
struct WalkState {
    LoadPolicy policy;
    std::filesystem::path root;                   // canonical project root
    std::vector<std::filesystem::path> visiting;  // active chain, innermost last
    std::vector<std::filesystem::path> visited;

    bool contains(const std::vector<std::filesystem::path>& set,
                  const std::filesystem::path& path) const {
        for (const auto& entry : set)
            if (entry == path) return true;
        return false;
    }
};

// The result of the guard every traversal has to pass before reading a
// manifest. Both walks share it: the cycle, escape and revisit rules are the
// hardening a review finding demanded, and one implementation is one place to
// get it right.
enum class Enter { ok, skip, error };

Enter enter_manifest(const std::filesystem::path& dir, WalkState& state,
                     std::filesystem::path& manifest, std::filesystem::path& canonical) {
    manifest = (dir / "mm.mdy").lexically_normal();

    if (!safe_exists(manifest)) {
        std::cerr << state.policy.tool << ": missing manifest: " << manifest.string() << "\n";
        return Enter::error;
    }

    // Resolves "..", "." and symlinks, so two spellings of one manifest compare
    // equal and a symlink loop cannot masquerade as a new directory.
    std::error_code ec;
    canonical = std::filesystem::weakly_canonical(manifest, ec);
    if (ec) {
        std::cerr << state.policy.tool << ": cannot resolve " << manifest.string() << ": " << ec.message() << "\n";
        return Enter::error;
    }

    const auto relative = canonical.lexically_relative(state.root);
    if (relative.empty() || *relative.begin() == "..") {
        std::cerr << state.policy.tool << ": manifest outside the project root: " << canonical.string() << "\n";
        return Enter::error;
    }

    if (state.contains(state.visiting, canonical)) {
        std::cerr << state.policy.tool << ": folder: cycle in the manifest tree:\n";
        for (const auto& entry : state.visiting)
            std::cerr << "    " << entry.lexically_relative(state.root).string() << "\n";
        std::cerr << "    " << relative.string() << "  <- repeats\n";
        return Enter::error;
    }

    if (state.policy.strict_tree) {
        // Generated lanes are root siblings: out is the bootstrap/configuration
        // tree, while configured host and target lanes use out-* names. Match
        // the complete first component so a source directory such as "outside"
        // is not rejected merely because its name begins with those letters.
        const auto first = relative.begin()->generic_string();
        if (first == "out" || first.starts_with("out-")) {
            std::cerr << state.policy.tool << ": generated output directory in manifest tree: "
                      << manifest.string() << "\n";
            return Enter::error;
        }
    }
    if (state.contains(state.visited, canonical)) {
        if (!state.policy.strict_tree) return Enter::skip;
        std::cerr << state.policy.tool << ": repeated canonical manifest directory: "
                  << manifest.string() << "\n";
        return Enter::error;
    }

    return Enter::ok;
}

// The kind and name every manifest must declare to be usable further,
// independent of whether the walk that reached it builds a BuildableNode (walk) or
// only records a ManifestNode (load_nodes' walk_nodes). Both walks share it: without
// this, load_nodes recorded whatever a manifest's front matter said, kind
// included, with no check that it named one of the nine kinds this project
// defines at all.
bool is_safe_name(std::string_view name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string_view::npos;
}

bool is_safe_relative_path(const std::filesystem::path& raw, const std::filesystem::path& joined) {
    if (raw.is_absolute()) return false;
    for (const auto& part : joined.lexically_normal())
        if (part == "..") return false;
    return true;
}

bool path_within(const std::filesystem::path& base, const std::filesystem::path& path) {
    const auto relative = path.lexically_normal().lexically_relative(base.lexically_normal());
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

// weakly_canonical resolves symlinks in whatever prefix of path already
// exists (a real fix for a symlink planted under out/ that would otherwise
// redirect a write outside the project), but when nothing on path exists
// at all it returns path unchanged and still relative rather than an
// absolute fallback, the bug fixed earlier by switching to absolute() +
// lexically_normal() alone. That case is safe to fall back to lexical
// resolution: a path cannot be escaped through a symlink that does not
// exist yet.
bool within_root(const std::filesystem::path& path) {
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec);
    if (ec) return false;

    auto resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec) return false;
    if (!resolved.is_absolute())
        resolved = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return false;

    const auto relative = resolved.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
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
        "cross-board-manifest", "cross-board-machine", "cross-board-linker-script",
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
    platform.compiler_arguments = all(document, "cross-board-argument");

    const bool has_board_details = !platform.machine.empty() || !platform.linker_script.empty() ||
                                   !platform.board_sources.empty() ||
                                   !platform.compiler_arguments.empty() ||
                                   lookup(document, "cross-board-provides") != nullptr;
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
        if (platform.link_ownership == mm::configure::LinkOwnership::External) {
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

bool valid_manifest(const mm::mdy::MDYDocument& doc, std::string_view kind, std::string_view name,
                    const std::filesystem::path& manifest, const LoadPolicy& policy) {
    if (!valid_mm_version(doc, manifest, policy)) return false;
    if (kind == "library" && manifest_version(first(doc, "mm"))->number < 13) {
        std::cerr << policy.tool << ": " << manifest.string()
                  << ": kind library requires mm: 1.3\n";
        return false;
    }
    if (kind != "project" && kind != "dir" && kind != "module" &&
        kind != "app" && kind != "test" && kind != "doc" &&
        kind != "sdk" && kind != "board" && kind != "library") {
        std::cerr << policy.tool << ": unknown kind \"" << kind << "\" in " << manifest.string() << "\n";
        return false;
    }
    if (name.empty()) {
        std::cerr << policy.tool << ": manifest has no name: " << manifest.string() << "\n";
        return false;
    }
    if (!is_safe_name(name)) {
        std::cerr << policy.tool << ": unsafe name \"" << name << "\" in " << manifest.string() << "\n";
        return false;
    }
    return true;
}

// A library's folder: entries reach the project-authored wrappers beside its
// vendored tree, never into it. file: being invalid on a library manifest keeps
// the manifest itself from naming foreign source, but it does not stop a folder:
// entry pointing at the checkout, and a manifest found there would produce
// ordinary targets that compile and are handed to check. The canonical
// comparison is what closes the door: a lexically unrelated folder: can still be
// a symlink into the tree.
bool folder_within_library_source(const std::filesystem::path& source,
                                  const std::filesystem::path& folder) {
    if (path_within(source, folder)) return true;

    // weakly_canonical leaves a path that does not exist yet relative, the same
    // case within_root documents; fall back to lexical resolution so the two
    // sides are always compared in the same form.
    const auto resolve = [](const std::filesystem::path& path) {
        std::error_code ec;
        auto resolved = std::filesystem::weakly_canonical(path, ec);
        if (ec) return std::filesystem::path{};
        if (!resolved.is_absolute())
            resolved = std::filesystem::absolute(path, ec).lexically_normal();
        return ec ? std::filesystem::path{} : resolved;
    };

    const auto canonical_source = resolve(source);
    const auto canonical_folder = resolve(folder);
    if (canonical_source.empty() || canonical_folder.empty()) return false;
    return path_within(canonical_source, canonical_folder);
}

// The one traversal: the structural node, the parsed document, and the
// target a manifest declares, all recorded from a single read of that
// manifest. walk and walk_nodes were separate recursive walkers doing the
// first two halves of this independently, which meant every caller wanting
// both read and parsed each manifest twice, with no guarantee the two reads
// saw the same bytes.
void walk_project(const std::filesystem::path& dir, std::size_t parent, Project& project,
                  WalkState& state) {
    std::filesystem::path manifest;
    std::filesystem::path canonical;

    switch (enter_manifest(dir, state, manifest, canonical)) {
        case Enter::error: project.ok = false; return;
        case Enter::skip:  return;
        case Enter::ok:    break;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest);
    const auto kind = first(doc, "kind");
    const auto name = first(doc, "name");

    if (!valid_manifest(doc, kind, name, manifest, state.policy)) {
        project.ok = false;
        state.visited.push_back(canonical);
        return;
    }

    ManifestNode node;
    node.manifest = manifest;
    node.dir = dir.lexically_normal();
    node.kind = kind;
    node.name = name;
    node.parent = parent;

    project.nodes.push_back(std::move(node));
    const auto index = project.nodes.size() - 1;

    // documents and target stay parallel to nodes, so every push here is
    // matched by one in each.
    project.documents.push_back(doc);
    project.target.push_back(no_target);

    if (parent != no_parent) project.nodes[parent].children.push_back(index);

    if (kind == "project" || kind == "dir" || kind == "library") {
        std::filesystem::path library_source;
        if (kind == "library") {
            const auto declared = first(doc, "source");
            if (!declared.empty()) library_source = (dir / declared).lexically_normal();
        }

        state.visiting.push_back(canonical);
        for (const auto& folder : all(doc, "folder")) {
            if (!library_source.empty() &&
                folder_within_library_source(library_source,
                                             (dir / folder).lexically_normal())) {
                std::cerr << state.policy.tool << ": " << manifest.string()
                          << ": folder is inside library source: " << folder << "\n";
                project.ok = false;
                continue;
            }
            walk_project(dir / folder, index, project, state);
        }
        state.visiting.pop_back();
        state.visited.push_back(canonical);
        return;
    }

    // A leaf manifest is finished the moment it is read, and must be marked
    // so before its target is built: a diamond reaches the same folder from
    // two parents, and only this stops the second visit building a second,
    // duplicate target for it.
    state.visited.push_back(canonical);

    if (kind == "sdk" || kind == "board") return;

    BuildableNode target;
    target.kind = kind;
    target.name = name;
    target.module_name = first(doc, "module");
    target.dir = dir.lexically_normal();
    target.uses = all(doc, "use");
    target.requires_board = first(doc, "requires-board");

    const auto push_source = [&](std::string_view value, bool join_with_dir) {
        auto unit = parse_unit(value);
        const std::filesystem::path raw = unit.path;
        const std::filesystem::path joined = join_with_dir ? dir / raw : raw;
        if (!is_safe_relative_path(raw, joined)) {
            std::cerr << state.policy.tool << ": unsafe source path \"" << unit.path << "\" in "
                      << manifest.string() << "\n";
            project.ok = false;
            return false;
        }
        unit.path = joined.lexically_normal().string();
        target.sources.push_back(std::move(unit));
        return true;
    };

    if (kind == "doc") {
        // Prose. Listed so a walk sees it, but nothing compiles or links it, so
        // an empty file: list is not an error.
        for (const auto& file : all(doc, "file"))
            if (!push_source(file, true)) return;

        project.docs.push_back(std::move(target));
        project.target[index] = project.docs.size() - 1;
        return;
    }

    if (kind == "test") {
        // unit: entries are already root relative.
        for (const auto& unit : all(doc, "unit"))
            if (!push_source(unit, false)) return;

        if (target.sources.empty()) {
            std::cerr << state.policy.tool << ": manifest declares no unit: entries: " << manifest.string() << "\n";
            project.ok = false;
            return;
        }

        project.tests.push_back(std::move(target));
        project.target[index] = project.tests.size() - 1;
        return;
    }

    // file: entries are relative to the manifest.
    for (const auto& file : all(doc, "file"))
        if (!push_source(file, true)) return;

    if (target.sources.empty()) {
        std::cerr << state.policy.tool << ": manifest declares no file: entries: " << manifest.string() << "\n";
        project.ok = false;
        return;
    }
    if (kind == "module" && target.module_name.empty()) {
        std::cerr << state.policy.tool << ": module manifest has no module: name: " << manifest.string() << "\n";
        project.ok = false;
        return;
    }

    project.targets.push_back(std::move(target));
    project.target[index] = project.targets.size() - 1;
}

bool definition_scalar(const mm::mdy::MDYDocument& doc, std::string_view key,
                       const std::filesystem::path& manifest, std::string& value,
                       std::string_view tool, bool required = true) {
    const auto* values = lookup(doc, key);
    if (values == nullptr) {
        if (!required) {
            value.clear();
            return true;
        }
        std::cerr << tool << ": " << manifest.string() << ": requires one " << key << "\n";
        return false;
    }
    if (values->size() != 1 || values->front().empty()) {
        std::cerr << tool << ": " << manifest.string() << ": requires one non-empty " << key
                  << "\n";
        return false;
    }
    value = values->front();
    return true;
}

std::optional<mm::configure::Responsibility> parse_responsibility(std::string_view value) {
    using R = mm::configure::Responsibility;
    if (value == "reset-vector") return R::ResetVector;
    if (value == "initial-stack") return R::InitialStack;
    if (value == "memory-layout") return R::MemoryLayout;
    if (value == "runtime-init") return R::RuntimeInit;
    if (value == "syscalls") return R::Syscalls;
    return std::nullopt;
}

bool definition_responsibilities(const mm::mdy::MDYDocument& doc,
                                 const std::filesystem::path& manifest,
                                 std::vector<mm::configure::Responsibility>& result,
                                 std::string_view tool) {
    std::set<mm::configure::Responsibility> seen;
    for (const auto& value : all(doc, "provides")) {
        const auto responsibility = parse_responsibility(value);
        if (!responsibility) {
            std::cerr << tool << ": " << manifest.string()
                      << ": unknown responsibility: " << value << "\n";
            return false;
        }
        if (!seen.insert(*responsibility).second) {
            std::cerr << tool << ": " << manifest.string()
                      << ": duplicate provides: " << value << "\n";
            return false;
        }
        result.push_back(*responsibility);
    }
    return true;
}

bool definition_path(const std::filesystem::path& root, const ManifestNode& node,
                     std::string_view raw, std::filesystem::path& result,
                     std::string_view key, std::string_view tool) {
    const std::filesystem::path relative(raw);
    const auto joined = node.dir / relative;
    if (!is_safe_relative_path(relative, joined)) {
        std::cerr << tool << ": " << node.manifest.string() << ": unsafe " << key << ": "
                  << raw << "\n";
        return false;
    }
    result = joined.lexically_normal();
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    const auto canonical_path = std::filesystem::weakly_canonical(root / result, ec);
    const auto inside = canonical_path.lexically_relative(canonical_root);
    if (ec || inside.empty() || *inside.begin() == ".." ||
        !std::filesystem::is_regular_file(canonical_path, ec) || ec) {
        std::cerr << tool << ": " << node.manifest.string() << ": " << key
                  << " is not a project file: " << raw << "\n";
        return false;
    }
    return true;
}

std::filesystem::path absolute_from_root(const std::filesystem::path& root,
                                        const std::filesystem::path& path) {
    return (path.is_absolute() ? path : root / path).lexically_normal();
}

std::filesystem::path relative_to_root(const std::filesystem::path& root,
                                      const std::filesystem::path& path) {
    return absolute_from_root(root, path).lexically_relative(root.lexically_normal());
}

bool observe_checkout(const std::filesystem::path& path, bool& present,
                      const std::filesystem::path& manifest, std::string_view tool) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        std::cerr << tool << ": " << manifest.string() << ": cannot inspect library source "
                  << path.string() << ": " << ec.message() << "\n";
        return false;
    }
    if (!exists) {
        present = false;
        return true;
    }
    const bool directory = std::filesystem::is_directory(path, ec);
    if (ec) {
        std::cerr << tool << ": " << manifest.string() << ": cannot inspect library source "
                  << path.string() << ": " << ec.message() << "\n";
        return false;
    }
    if (!directory) {
        present = false;
        return true;
    }
    const auto begin = std::filesystem::directory_iterator(path, ec);
    if (ec) {
        std::cerr << tool << ": " << manifest.string() << ": cannot read library source "
                  << path.string() << ": " << ec.message() << "\n";
        return false;
    }
    present = begin != std::filesystem::directory_iterator{};
    return true;
}

bool link_input_name(std::string_view value) {
    if (value.empty()) return false;
    const auto first_valid = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    const auto rest_valid = [&](char c) {
        return first_valid(c) || c == '.' || c == '+' || c == '-';
    };
    if (!first_valid(value.front())) return false;
    for (const auto c : value.substr(1))
        if (!rest_valid(c)) return false;
    return true;
}

bool library_interface_paths(const std::filesystem::path& root, const ManifestNode& node,
                             const mm::mdy::MDYDocument& doc, LibraryDefinition& library,
                             std::string_view tool) {
    const auto add_paths = [&](std::string_view key, std::vector<LibraryPath>& destination) {
        for (const auto& value : all(doc, key)) {
            const std::filesystem::path raw(value);
            const auto normalized = raw.lexically_normal();
            if (raw.empty() || raw.is_absolute() || normalized.empty() ||
                *normalized.begin() == "..") {
                std::cerr << tool << ": " << node.manifest.string() << ": unsafe " << key
                          << ": " << value << "\n";
                return false;
            }
            destination.push_back(
                {LibraryPathBase::Source, (library.source / normalized).lexically_normal()});
        }
        return true;
    };

    if (!add_paths("include-directory", library.include_directories) ||
        !add_paths("library-directory", library.library_directories) ||
        !add_paths("link-archive", library.link_archives))
        return false;

    for (const auto& value : all(doc, "link-input")) {
        if (!link_input_name(value)) {
            std::cerr << tool << ": " << node.manifest.string() << ": invalid link-input: "
                      << value << " (expected [A-Za-z0-9_][A-Za-z0-9_.+-]*)\n";
            return false;
        }
        library.link_inputs.push_back(value);
    }

    if (!library.checkout_present) return true;

    std::error_code ec;
    const auto source = std::filesystem::weakly_canonical(
        absolute_from_root(root, library.source), ec);
    if (ec) {
        std::cerr << tool << ": " << node.manifest.string()
                  << ": cannot resolve library source: " << ec.message() << "\n";
        return false;
    }
    for (const auto* paths : {&library.include_directories, &library.library_directories,
                              &library.link_archives}) {
        for (const auto& entry : *paths) {
            const auto candidate = absolute_from_root(root, entry.path);
            const bool exists = std::filesystem::exists(candidate, ec);
            if (ec) {
                std::cerr << tool << ": " << node.manifest.string()
                          << ": cannot inspect library interface path " << candidate.string()
                          << ": " << ec.message() << "\n";
                return false;
            }
            if (!exists) continue;
            const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
            if (ec || !path_within(source, canonical)) {
                std::cerr << tool << ": " << node.manifest.string()
                          << ": library interface path escapes source: "
                          << entry.path.string() << "\n";
                return false;
            }
        }
    }
    const auto licence = std::filesystem::weakly_canonical(
        absolute_from_root(root, library.licence), ec);
    if (ec || path_within(source, licence)) {
        std::cerr << tool << ": " << node.manifest.string()
                  << ": licence must resolve outside library source\n";
        return false;
    }
    return true;
}

bool parse_definitions(Project& project, const std::filesystem::path& root,
                       const LoadPolicy& policy) {
    std::map<std::string, std::filesystem::path, std::less<>> sdk_names;
    std::map<std::string, std::filesystem::path, std::less<>> board_names;
    std::map<std::string, std::filesystem::path, std::less<>> library_names;

    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        const auto& doc = project.documents[i];
        if (node.kind == "sdk") {
            SdkDefinition sdk;
            sdk.node = i;
            sdk.name = node.name;
            sdk.manifest = node.manifest;
            std::string family;
            std::string runtime;
            std::string sysroot;
            std::string runtime_prefix;
            if (!definition_scalar(doc, "target", node.manifest, sdk.target, policy.tool) ||
                !definition_scalar(doc, "compiler-family", node.manifest, family, policy.tool) ||
                !definition_scalar(doc, "runtime", node.manifest, runtime, policy.tool) ||
                !definition_scalar(doc, "specs-profile", node.manifest, sdk.specs_profile,
                                   policy.tool, false) ||
                !definition_scalar(doc, "library", node.manifest, sdk.library,
                                   policy.tool, false) ||
                !definition_scalar(doc, "sysroot", node.manifest, sysroot,
                                   policy.tool, false) ||
                !definition_scalar(doc, "runtime-prefix", node.manifest, runtime_prefix,
                                   policy.tool, false))
                return false;
            if (family == "gcc") sdk.family = CompilerFamily::Gcc;
            else if (family == "clang") sdk.family = CompilerFamily::Clang;
            else {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": compiler-family must be gcc or clang\n";
                return false;
            }
            if (runtime == "glibc") sdk.runtime = mm::configure::PlatformRuntime::Glibc;
            else if (runtime == "newlib") sdk.runtime = mm::configure::PlatformRuntime::Newlib;
            else if (runtime == "picolibc") sdk.runtime = mm::configure::PlatformRuntime::Picolibc;
            else if (runtime == "none") sdk.runtime = mm::configure::PlatformRuntime::None;
            else {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": unsupported runtime: " << runtime << "\n";
                return false;
            }
            if (!mm::configure::target_system(sdk.target)) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": unsupported target: " << sdk.target << "\n";
                return false;
            }
            const auto* specs_file_values = lookup(doc, "specs-file");
            if (!sdk.specs_profile.empty() && specs_file_values != nullptr) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": specs-profile and specs-file are mutually exclusive\n";
                return false;
            }
            if (specs_file_values != nullptr) {
                std::string specs_file;
                if (!definition_scalar(doc, "specs-file", node.manifest, specs_file, policy.tool) ||
                    !definition_path(root, node, specs_file, sdk.specs_file, "specs-file", policy.tool))
                    return false;
            }
            for (const auto& pair : {std::pair{std::string_view("sysroot"), &sysroot},
                                     std::pair{std::string_view("runtime-prefix"), &runtime_prefix}}) {
                if (pair.second->empty()) continue;
                std::filesystem::path path(*pair.second);
                if (!path.is_absolute()) {
                    std::cerr << policy.tool << ": " << node.manifest.string() << ": "
                              << pair.first << " must be absolute\n";
                    return false;
                }
                if (pair.first == "sysroot") sdk.sysroot = path;
                else sdk.runtime_prefix = path;
            }
            if (!definition_responsibilities(doc, node.manifest, sdk.provides, policy.tool))
                return false;
            const bool hosted = *mm::configure::target_system(sdk.target) ==
                                mm::configure::PlatformSystem::Linux;
            if (hosted && !sdk.provides.empty()) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": hosted SDK cannot declare provides\n";
                return false;
            }
            if (!sdk.specs_profile.empty() && !sdk.provides.empty()) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": specs-profile SDK cannot declare provides\n";
                return false;
            }
            if (!sdk.specs_file.empty()) {
                std::set<mm::configure::Responsibility> supplied(sdk.provides.begin(), sdk.provides.end());
                if (!supplied.contains(mm::configure::Responsibility::RuntimeInit) ||
                    !supplied.contains(mm::configure::Responsibility::Syscalls) || supplied.size() != 2) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": specs-file SDK must provide runtime-init and syscalls\n";
                    return false;
                }
            }
            if (!sdk.specs_profile.empty() &&
                !(sdk.family == CompilerFamily::Gcc && sdk.target == "arm-none-eabi" &&
                  sdk.specs_profile == "rdimon")) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": unknown specs profile: " << sdk.specs_profile << "\n";
                return false;
            }
            const auto duplicate = sdk_names.find(sdk.name);
            if (duplicate != sdk_names.end()) {
                std::cerr << policy.tool << ": SDK name \"" << sdk.name
                          << "\" is declared by both " << duplicate->second.string() << " and "
                          << sdk.manifest.string() << "\n";
                return false;
            }
            sdk_names[sdk.name] = sdk.manifest;
            project.sdks.push_back(std::move(sdk));
        } else if (node.kind == "board") {
            BoardDefinition board;
            board.node = i;
            board.name = node.name;
            board.manifest = node.manifest;
            std::string linker;
            if (!definition_scalar(doc, "sdk", node.manifest, board.sdk, policy.tool) ||
                !definition_scalar(doc, "cpu", node.manifest, board.cpu, policy.tool) ||
                !definition_scalar(doc, "instruction-set", node.manifest,
                                   board.instruction_set, policy.tool) ||
                !definition_scalar(doc, "float-abi", node.manifest, board.float_abi,
                                   policy.tool) ||
                !definition_scalar(doc, "machine", node.manifest, board.machine,
                                   policy.tool, false) ||
                !definition_scalar(doc, "linker-script", node.manifest, linker,
                                   policy.tool, false))
                return false;
            if (!linker.empty() &&
                !definition_path(root, node, linker, board.linker_script,
                                 "linker-script", policy.tool))
                return false;
            for (const auto& source : all(doc, "file")) {
                std::filesystem::path path;
                if (!definition_path(root, node, source, path, "file", policy.tool)) return false;
                board.sources.push_back(std::move(path));
            }
            if (!definition_responsibilities(doc, node.manifest, board.provides, policy.tool))
                return false;
            const auto duplicate = board_names.find(board.name);
            if (duplicate != board_names.end()) {
                std::cerr << policy.tool << ": board name \"" << board.name
                          << "\" is declared by both " << duplicate->second.string() << " and "
                          << board.manifest.string() << "\n";
                return false;
            }
            board_names[board.name] = board.manifest;
            project.boards.push_back(std::move(board));
        } else if (node.kind == "library") {
            LibraryDefinition library;
            library.node = i;
            library.name = node.name;
            library.manifest = node.manifest;
            std::string source;
            std::string licence;
            if (!definition_scalar(doc, "source", node.manifest, source, policy.tool) ||
                !definition_scalar(doc, "licence", node.manifest, licence, policy.tool))
                return false;

            const auto source_path = (node.dir / source).lexically_normal();
            const auto absolute_source = absolute_from_root(root, source_path);
            if (std::filesystem::path(source).is_absolute() ||
                !path_within(root.lexically_normal(), absolute_source)) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": source is outside the project: " << source << "\n";
                return false;
            }
            library.source = relative_to_root(root, source_path);

            std::filesystem::path licence_path;
            if (!definition_path(root, node, licence, licence_path, "licence", policy.tool))
                return false;
            library.licence = relative_to_root(root, licence_path);
            if (path_within(absolute_source, absolute_from_root(root, library.licence))) {
                std::cerr << policy.tool << ": " << node.manifest.string()
                          << ": licence must be outside library source\n";
                return false;
            }

            if (!definition_scalar(doc, "external-build", node.manifest,
                                   library.external_build, policy.tool, false))
                return false;
            if (!library.external_build.empty()) {
                if (library.external_build != "cmake") {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": unknown external-build: " << library.external_build << "\n";
                    return false;
                }
                const auto cmake_dir = (node.dir / "cmake").lexically_normal();
                const auto absolute_cmake = absolute_from_root(root, cmake_dir);
                std::error_code ec;
                const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
                if (ec) return false;
                const auto canonical_cmake = std::filesystem::weakly_canonical(absolute_cmake, ec);
                if (ec || !path_within(canonical_root, canonical_cmake)) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": cmake directory is outside the project: "
                              << cmake_dir.generic_string() << "\n";
                    return false;
                }
                const bool cmake_is_dir = std::filesystem::is_directory(canonical_cmake, ec);
                if (ec || !cmake_is_dir) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": external-build library requires a cmake directory beside its manifest\n";
                    return false;
                }
                const auto cmakelists = absolute_cmake / "CMakeLists.txt";
                const auto canonical_cmakelists = std::filesystem::weakly_canonical(cmakelists, ec);
                const bool cmakelists_is_file = std::filesystem::is_regular_file(canonical_cmakelists, ec);
                if (ec || !path_within(canonical_root, canonical_cmakelists) || !cmakelists_is_file) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": external-build library requires cmake/CMakeLists.txt\n";
                    return false;
                }
                const auto canonical_source = std::filesystem::weakly_canonical(absolute_source, ec);
                if (ec || path_within(canonical_source, canonical_cmake) ||
                    path_within(canonical_source, canonical_cmakelists)) {
                    std::cerr << policy.tool << ": " << node.manifest.string()
                              << ": cmake directory must resolve outside library source\n";
                    return false;
                }
            }

            if (!observe_checkout(absolute_source, library.checkout_present,
                                  node.manifest, policy.tool) ||
                !library_interface_paths(root, node, doc, library, policy.tool))
                return false;

            const auto duplicate = library_names.find(library.name);
            if (duplicate != library_names.end()) {
                std::cerr << policy.tool << ": library name \"" << library.name
                          << "\" is declared by both " << duplicate->second.string() << " and "
                          << library.manifest.string() << "\n";
                return false;
            }
            library_names[library.name] = library.manifest;
            project.libraries.push_back(std::move(library));
        }
    }

    for (const auto& sdk : project.sdks) {
        if (sdk.library.empty()) continue;
        const LibraryDefinition* library = nullptr;
        for (const auto& candidate : project.libraries)
            if (candidate.name == sdk.library) library = &candidate;
        if (library == nullptr) {
            std::cerr << policy.tool << ": " << sdk.manifest.string()
                      << ": SDK references unknown library: " << sdk.library;
            if (!project.libraries.empty()) {
                std::cerr << " (available:";
                for (const auto& candidate_lib : project.libraries) std::cerr << " " << candidate_lib.name;
                std::cerr << ")";
            }
            std::cerr << "\n";
            return false;
        }
        if (!library->external_build.empty()) {
            if (!sdk.specs_profile.empty()) {
                std::cerr << policy.tool << ": " << sdk.manifest.string()
                          << ": SDK whose library declares external-build cannot declare specs-profile\n";
                return false;
            }
            if (!sdk.specs_file.empty()) {
                std::cerr << policy.tool << ": " << sdk.manifest.string()
                          << ": SDK whose library declares external-build cannot declare specs-file\n";
                return false;
            }
        }
    }

    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        if (node.kind != "module") continue;

        auto& target = project.targets[project.target[i]];
        if (!definition_scalar(project.documents[i], "library", node.manifest,
                               target.library, policy.tool, false))
            return false;
        if (target.library.empty()) continue;

        bool found = false;
        for (const auto& library : project.libraries)
            if (library.name == target.library) found = true;
        if (found) continue;

        std::cerr << policy.tool << ": " << node.manifest.string()
                  << ": module references unknown library: " << target.library;
        if (!project.libraries.empty()) {
            std::cerr << " (available:";
            for (const auto& library : project.libraries) std::cerr << " " << library.name;
            std::cerr << ")";
        }
        std::cerr << "\n";
        return false;
    }

    for (const auto& board : project.boards) {
        const SdkDefinition* sdk = nullptr;
        for (const auto& candidate : project.sdks)
            if (candidate.name == board.sdk) sdk = &candidate;
        if (sdk == nullptr) {
            std::cerr << policy.tool << ": " << board.manifest.string()
                      << ": board references unknown SDK: " << board.sdk << "\n";
            return false;
        }
        if (find_processor_entry(sdk->family, sdk->target, board.cpu,
                                 board.instruction_set, board.float_abi) == nullptr) {
            std::cerr << policy.tool << ": " << board.manifest.string()
                      << ": unknown processor combination for SDK " << sdk->name << "\n";
            return false;
        }
        const LibraryDefinition* library = nullptr;
        if (!sdk->library.empty()) {
            for (const auto& candidate : project.libraries)
                if (candidate.name == sdk->library) library = &candidate;
        }
        const bool external = library != nullptr && !library->external_build.empty();
        if (!external) {
            if (board.linker_script.empty()) {
                std::cerr << policy.tool << ": " << board.manifest.string()
                          << ": board requires linker-script\n";
                return false;
            }
            if (board.sources.empty()) {
                std::cerr << policy.tool << ": " << board.manifest.string()
                          << ": board requires at least one file\n";
                return false;
            }
        }
    }
    return true;
}


std::size_t index_of_module(const Tree& tree, const std::string& module_name) {
    for (std::size_t i = 0; i < tree.targets.size(); ++i)
        if (tree.targets[i].kind == "module" && tree.targets[i].module_name == module_name)
            return i;
    return tree.targets.size();
}

bool order_visit(std::size_t index, const Tree& tree,
                 std::vector<int>& state, std::vector<std::size_t>& out) {
    if (state[index] == 2) return true;
    if (state[index] == 1) {
        std::cerr << "build: dependency cycle through " << tree.targets[index].name << "\n";
        return false;
    }

    state[index] = 1;

    for (const auto& used : tree.targets[index].uses) {
        const auto dependency = index_of_module(tree, used);
        if (dependency == tree.targets.size()) {
            std::cerr << "build: " << tree.targets[index].name
                      << " uses unknown module " << used << "\n";
            return false;
        }
        if (!order_visit(dependency, tree, state, out)) return false;
    }

    state[index] = 2;
    out.push_back(index);
    return true;
}

void closure_visit(std::size_t index, const Tree& tree,
                   std::vector<bool>& seen, std::vector<std::filesystem::path>& out) {
    if (seen[index]) return;
    seen[index] = true;

    for (const auto& object : tree.targets[index].objects) out.push_back(object);

    for (const auto& used : tree.targets[index].uses) {
        const auto dependency = index_of_module(tree, used);
        if (dependency != tree.targets.size()) closure_visit(dependency, tree, seen, out);
    }
}

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

TranslationUnit parse_unit(std::string_view value) {
    TranslationUnit unit;

    const auto split = value.find_first_of(" \t");
    if (split == std::string_view::npos) {
        unit.path = std::string(value);
        return unit;
    }

    unit.path = std::string(value.substr(0, split));

    const auto name = value.find_first_not_of(" \t", split);
    if (name != std::string_view::npos) unit.module_name = std::string(value.substr(name));

    return unit;
}

StructuralProperties structural_properties(
    const std::vector<mm::configure::OptionValues>& resolved) {
    StructuralProperties properties;
    properties.nodes.reserve(resolved.size());
    const auto property = [](const mm::configure::OptionValue& value) {
        return StructuralProperty{value.boolean, value.origin, value.value_source,
                                  value.read_only, value.lock_source};
    };
    for (const auto& values : resolved) {
        properties.nodes.push_back({property(values.find("buildable-host")->second),
                                    property(values.find("buildable-target")->second),
                                    property(values.find("core")->second)});
    }
    return properties;
}

bool validate_structural_properties(
    const std::vector<mm::configure::OptionNode>& nodes,
    const StructuralProperties& properties, std::string_view tool) {
    if (nodes.size() != properties.nodes.size()) return false;

    // use: names a module, and the resolver is indexed by node, so the edge
    // needs a module name to node index map. index_of_module answers with a
    // targets index instead, which is the wrong side of the join here.
    std::map<std::string, std::size_t, std::less<>> modules;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == "module" && !nodes[i].module_name.empty())
            modules.emplace(nodes[i].module_name, i);

    bool ok = true;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind != "sdk" && properties.nodes[i].core.value && !nodes[i].library.empty()) {
            std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                      << nodes[i].name << " is core, but declares library "
                      << nodes[i].library << "\n";
            ok = false;
        }
        for (const auto& used : nodes[i].uses) {
            const auto found = modules.find(used);
            if (found == modules.end()) continue;  // order() reports unknown modules
            for (const auto lane : {false, true}) {
                const auto& consumer = lane ? properties.nodes[i].buildable_target
                                            : properties.nodes[i].buildable_host;
                const auto& dependency = lane ? properties.nodes[found->second].buildable_target
                                              : properties.nodes[found->second].buildable_host;
                if (!consumer.value || dependency.value) continue;
                const auto name = lane ? "buildable-target" : "buildable-host";
                std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                          << nodes[i].name << " is " << name << ", but " << used
                          << " is not (" << name << " no, "
                          << (dependency.origin == mm::configure::OptionOrigin::Assignment
                                  ? "assigned by " : "reset by ")
                          << dependency.value_source.generic_string() << ")\n";
                ok = false;
            }

            const auto& consumer_core = properties.nodes[i].core;
            const auto& dependency_core = properties.nodes[found->second].core;
            if (consumer_core.value && !dependency_core.value) {
                std::cerr << tool << ": " << nodes[i].manifest.generic_string() << ": "
                          << nodes[i].name << " is core, but " << used
                          << " is not (core no, "
                          << (dependency_core.origin == mm::configure::OptionOrigin::Assignment
                                  ? "assigned by " : "reset by ")
                          << dependency_core.value_source.generic_string() << ")\n";
                ok = false;
            }
        }
    }
    return ok;
}

std::filesystem::path resolve_manifest(std::filesystem::path path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) path /= "mm.mdy";
    return path;
}

std::filesystem::path find_project_root(std::filesystem::path dir) {
    for (; !dir.empty(); dir = dir.parent_path()) {
        const auto candidate = dir / "mm.mdy";
        if (safe_exists(candidate) &&
            first(mm::mdy::Parser::parse_file(candidate), "kind") == "project") {
            return dir;
        }
        if (!dir.has_relative_path()) break;
    }
    return {};
}

Project load_project(const std::filesystem::path& dir, const LoadPolicy& policy) {
    Project project;

    std::error_code ec;
    WalkState state;
    state.policy = policy;
    state.root = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        std::cerr << policy.tool << ": cannot resolve project root " << dir.string()
                  << ": " << ec.message() << "\n";
        project.ok = false;
        return project;
    }

    walk_project(dir, no_parent, project, state);
    if (!project.ok) return project;
    if (!parse_definitions(project, state.root, policy)) {
        project.ok = false;
        return project;
    }

    // Checked once the whole tree is built, not incrementally during the
    // walk, since a duplicate can only be found once every candidate name
    // is known. Without this, index_of_module returns whichever target
    // with a given module: name it happens to see first, silently treating
    // two real, different modules as interchangeable; two app: targets with
    // the same name would silently install() one over the other under
    // out/bin; and nothing at all currently rules out two different
    // manifests claiming the same directory.
    std::map<std::string, const BuildableNode*, std::less<>> modules_by_name;
    std::map<std::string, const BuildableNode*, std::less<>> apps_by_name;
    std::map<std::filesystem::path, const BuildableNode*> targets_by_dir;

    auto check_dir = [&](const BuildableNode& target) {
        const auto it = targets_by_dir.find(target.dir);
        if (it != targets_by_dir.end()) {
            std::cerr << policy.tool << ": " << target.dir.string() << " is declared by more than one manifest: "
                      << it->second->name << " and " << target.name << "\n";
            project.ok = false;
            return;
        }
        targets_by_dir[target.dir] = &target;
    };

    for (const auto& target : project.targets) {
        check_dir(target);

        if (target.kind == "module") {
            const auto it = modules_by_name.find(target.module_name);
            if (it != modules_by_name.end()) {
                std::cerr << policy.tool << ": module: " << target.module_name << " is exported by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                project.ok = false;
            } else {
                modules_by_name[target.module_name] = &target;
            }
        } else if (target.kind == "app") {
            const auto it = apps_by_name.find(target.name);
            if (it != apps_by_name.end()) {
                std::cerr << policy.tool << ": app name \"" << target.name << "\" is declared by both "
                          << it->second->dir.string() << " and " << target.dir.string() << "\n";
                project.ok = false;
            } else {
                apps_by_name[target.name] = &target;
            }
        }
    }

    for (const auto& target : project.tests) check_dir(target);
    for (const auto& target : project.docs) check_dir(target);

    return project;
}


std::vector<mm::configure::OptionNode> configuration_nodes(const Project& project) {
    std::vector<mm::configure::OptionNode> nodes;
    if (!project.ok || project.nodes.size() != project.documents.size()) return nodes;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto& node = project.nodes[i];
        const auto& doc = project.documents[i];
        nodes.push_back({node.manifest, node.dir, node.name, node.kind,
                         first(doc, "module"), all(doc, "use"), node.parent,
                         all(doc, "option"), all(doc, "reset"), all(doc, "read-only"),
                         first(doc, "library")});
    }
    return nodes;
}

bool resolve_structural_properties(const std::filesystem::path& project_root, Build build,
                                   const Project& project, StructuralProperties& properties,
                                   std::string_view tool) {
    auto nodes = configuration_nodes(project);
    for (auto& node : nodes) {
        node.options = structural_property_declarations(node.options);
        node.resets = structural_property_declarations(node.resets);
        node.read_only = structural_property_declarations(node.read_only);
    }

    std::vector<mm::configure::OptionValues> resolved;
    if (!mm::configure::resolve_options(project_root, build, nodes, resolved, tool))
        return false;
    properties = structural_properties(resolved);
    return validate_structural_properties(nodes, properties, tool);
}

std::vector<bool> StructuralProperties::lane(bool target_lane,
                                             bool target_has_host_capability) const {
    std::vector<bool> result;
    result.reserve(nodes.size());
    if (!target_lane) {
        for (const auto& node : nodes) result.push_back(node.buildable_host.value);
        return result;
    }
    if (!target_has_host_capability) {
        for (const auto& node : nodes) result.push_back(node.buildable_target.value);
        return result;
    }

    for (const auto& node : nodes)
        result.push_back(node.buildable_target.value || node.buildable_host.value);
    return result;
}

bool validate_library_checkout(const std::filesystem::path& project_root,
                               const LibraryDefinition& library, std::string_view tool) {
    const auto source = absolute_from_root(project_root, library.source);
    bool present = false;
    if (!observe_checkout(source, present, library.manifest, tool)) return false;
    if (!present) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(source, ec);
        if (ec) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": cannot inspect library source " << source.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        const bool directory = exists && std::filesystem::is_directory(source, ec);
        if (ec) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": cannot inspect library source " << source.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (exists && !directory) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": library source is not a directory: " << source.string() << "\n";
            return false;
        }
        std::cerr << tool << ": library " << library.name << " checkout is absent: "
                  << library.source.generic_string()
                  << "; run git submodule update --init --recursive -- "
                  << shell_quote(library.source) << "\n";
        return false;
    }

    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(project_root, ec);
    if (ec) return false;
    const auto canonical_source = std::filesystem::weakly_canonical(source, ec);
    if (ec || !path_within(root, canonical_source)) {
        std::cerr << tool << ": " << library.manifest.string()
                  << ": library source resolves outside the project: "
                  << library.source.generic_string() << "\n";
        return false;
    }
    for (const auto* paths : {&library.include_directories, &library.library_directories,
                              &library.link_archives}) {
        for (const auto& entry : *paths) {
            const auto candidate = absolute_from_root(project_root, entry.path);
            const bool exists = std::filesystem::exists(candidate, ec);
            if (ec) {
                std::cerr << tool << ": " << library.manifest.string()
                          << ": cannot inspect library interface path " << candidate.string()
                          << ": " << ec.message() << "\n";
                return false;
            }
            if (!exists) continue;
            const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
            if (ec || !path_within(canonical_source, canonical)) {
                std::cerr << tool << ": " << library.manifest.string()
                          << ": library interface path escapes source: "
                          << entry.path.generic_string() << "\n";
                return false;
            }
        }
    }
    const auto licence = std::filesystem::weakly_canonical(
        absolute_from_root(project_root, library.licence), ec);
    if (ec || path_within(canonical_source, licence)) {
        std::cerr << tool << ": " << library.manifest.string()
                  << ": licence must resolve outside library source\n";
        return false;
    }
    if (!library.external_build.empty()) {
        const auto cmake_dir = std::filesystem::weakly_canonical(
            absolute_from_root(project_root, library.manifest.parent_path() / "cmake"), ec);
        if (ec || path_within(canonical_source, cmake_dir)) {
            std::cerr << tool << ": " << library.manifest.string()
                      << ": cmake directory must resolve outside library source\n";
            return false;
        }
    }
    return true;
}

bool library_include_directories(
    const std::filesystem::path& project_root,
    const std::vector<LibraryDefinition>& libraries,
    const BuildableNode& target,
    std::vector<std::filesystem::path>& directories,
    std::string_view tool) {
    directories.clear();
    if (target.library.empty()) return true;

    const LibraryDefinition* definition = nullptr;
    for (const auto& library : libraries)
        if (library.name == target.library) definition = &library;
    if (definition == nullptr) {
        std::cerr << tool << ": " << (target.dir / "mm.mdy").string()
                  << ": module references unknown library: " << target.library << "\n";
        return false;
    }

    std::error_code ec;
    const auto root = std::filesystem::absolute(project_root, ec).lexically_normal();
    if (ec) {
        std::cerr << tool << ": cannot resolve project root " << project_root.string()
                  << ": " << ec.message() << "\n";
        return false;
    }

    if (!validate_library_checkout(root, *definition, tool)) {
        // The checkout validator reports the resource failure and recovery
        // command; this second diagnostic deliberately names its consumer.
        std::cerr << tool << ": " << (target.dir / "mm.mdy").string()
                  << ": module " << target.name << " cannot use library "
                  << definition->name << "\n";
        return false;
    }

    for (const auto& include : definition->include_directories) {
        // Stage 2 constructs Source entries only. This guard becomes reachable
        // when the external-build stage introduces BuildPrefix interfaces.
        if (include.base != LibraryPathBase::Source) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": build-prefix include directory has no producer\n";
            return false;
        }
        const auto path = absolute_from_root(root, include.path);
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": cannot inspect library include directory " << path.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (!exists) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": library include directory does not exist: "
                      << include.path.generic_string() << "\n";
            return false;
        }
        const bool directory = std::filesystem::is_directory(path, ec);
        if (ec) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": cannot inspect library include directory " << path.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (!directory) {
            std::cerr << tool << ": " << definition->manifest.string()
                      << ": library include path is not a directory: "
                      << include.path.generic_string() << "\n";
            return false;
        }
        directories.push_back(path);
    }
    return true;
}

Availability availability(const Project& project, std::size_t node, bool capability,
                          bool target_lane, const Platform* platform) {
    if (node >= project.nodes.size()) return {false, "manifest node is not registered"};
    if (!capability) {
        return {false, project.nodes[node].name + " is not buildable-" +
                           (target_lane ? "target" : "host")};
    }
    if (project.nodes[node].kind == "module" && project.target[node] != no_target) {
        const auto& target = project.targets[project.target[node]];
        if (!target.library.empty()) {
            const LibraryDefinition* library = nullptr;
            for (const auto& candidate : project.libraries) {
                if (candidate.name == target.library) {
                    library = &candidate;
                    break;
                }
            }
            if (library != nullptr && !library->external_build.empty()) {
                const SdkDefinition* selected_sdk = nullptr;
                if (target_lane && platform != nullptr && platform->sdk) {
                    for (const auto& candidate : project.sdks) {
                        if (candidate.name == *platform->sdk) {
                            selected_sdk = &candidate;
                            break;
                        }
                    }
                }
                if (selected_sdk == nullptr || selected_sdk->library != library->name) {
                    return {false, project.nodes[node].name + " requires an SDK naming library " +
                                       library->name};
                }
            }
        }
    }
    if (!target_lane || project.target[node] == no_target) return {true, {}};

    const BuildableNode* buildable = nullptr;
    if (project.nodes[node].kind == "test")
        buildable = &project.tests[project.target[node]];
    else if (project.nodes[node].kind == "app" || project.nodes[node].kind == "module")
        buildable = &project.targets[project.target[node]];
    if (buildable == nullptr || buildable->requires_board.empty()) return {true, {}};

    const std::string selected = platform != nullptr && platform->board
                                     ? *platform->board
                                     : std::string("none");
    if (selected == buildable->requires_board) return {true, {}};
    return {false, buildable->name + " requires board " + buildable->requires_board +
                       "; selected board is " + selected};
}

bool can_link_executable(const Platform* platform, std::string_view tool,
                         std::string_view name) {
    if (platform == nullptr || !platform->models_responsibilities || platform->unresolved.empty())
        return true;
    std::cerr << tool << ": " << name << ": unresolved platform responsibility: "
              << mm::configure::responsibility_name(platform->unresolved.front()) << "\n";
    return false;
}

bool check_configuration_staleness(const BuildConfiguration& configuration,
                                   const Project& project,
                                   bool target_lane,
                                   std::string_view tool) {
    if (!target_lane) return true;
    const auto* platform = configuration.configured_target_platform();
    if (platform == nullptr || !platform->sdk) return true;

    const SdkDefinition* sdk = nullptr;
    for (const auto& entry : project.sdks) {
        if (entry.name == *platform->sdk) {
            sdk = &entry;
            break;
        }
    }
    if (sdk == nullptr) {
        std::cerr << tool << ": stale configuration record: selected SDK \"" << *platform->sdk
                  << "\" is not in the project; rerun configure\n";
        return false;
    }

    const LibraryDefinition* library = nullptr;
    if (!sdk->library.empty()) {
        for (const auto& lib : project.libraries) {
            if (lib.name == sdk->library) {
                library = &lib;
                break;
            }
        }
        if (library == nullptr) {
            std::cerr << tool << ": stale configuration record: library \"" << sdk->library
                      << "\" is not in the project; rerun configure\n";
            return false;
        }
    }

    const auto expected = (library != nullptr && !library->external_build.empty())
                              ? mm::configure::LinkOwnership::External
                              : mm::configure::LinkOwnership::Project;

    if (platform->link_ownership != expected) {
        if (library != nullptr) {
            std::cerr << tool << ": stale configuration record: library \""
                      << library->name
                      << "\" external-build declaration does not match cross-link; rerun configure\n";
        } else {
            std::cerr << tool << ": stale configuration record: SDK \""
                      << sdk->name
                      << "\" names no external library; rerun configure\n";
        }
        return false;
    }
    return true;
}

std::optional<BuildableNode> platform_unit(const Platform* platform) {
    if (platform == nullptr || !platform->board || platform->board_sources.empty())
        return std::nullopt;
    BuildableNode unit;
    unit.kind = "board";
    unit.name = *platform->board;
    if (platform->board_manifest) unit.dir = platform->board_manifest->parent_path();
    for (const auto& source : platform->board_sources)
        unit.sources.push_back({source.generic_string(), {}});
    return unit;
}

// Projections of the single traversal above, kept so callers that want only
// one view need not know about the other.
Tree load_tree(const std::filesystem::path& dir, const LoadPolicy& policy) {
    auto project = load_project(dir, policy);

    Tree tree;
    tree.ok = project.ok;
    tree.targets = std::move(project.targets);
    tree.tests = std::move(project.tests);
    tree.docs = std::move(project.docs);
    return tree;
}

std::vector<ManifestNode> load_nodes(const std::filesystem::path& dir, bool& ok,
                                     const LoadPolicy& policy) {
    auto project = load_project(dir, policy);
    ok = project.ok;
    return std::move(project.nodes);
}

BuildableNode load_test(const std::filesystem::path& manifest_path, bool& ok,
                        const LoadPolicy& policy) {
    ok = false;
    BuildableNode target;

    if (!safe_exists(manifest_path)) {
        std::cerr << policy.tool << ": manifest does not exist: " << manifest_path.string() << "\n";
        return target;
    }

    const auto doc = mm::mdy::Parser::parse_file(manifest_path);

    if (!valid_manifest(doc, first(doc, "kind"), first(doc, "name"), manifest_path, policy))
        return target;

    const auto kind = first(doc, "kind");
    if (kind != "test") {
        std::cerr << policy.tool << ": manifest kind is \"" << kind << "\", expected \"test\"\n";
        return target;
    }

    target.kind = kind;
    target.name = first(doc, "name");
    target.module_name = first(doc, "module");
    target.dir = manifest_path.parent_path();
    target.uses = all(doc, "use");
    target.requires_board = first(doc, "requires-board");

    if (target.name.empty()) {
        std::cerr << policy.tool << ": manifest has no name\n";
        return target;
    }
    if (!is_safe_name(target.name)) {
        std::cerr << policy.tool << ": unsafe name \"" << target.name << "\"\n";
        return target;
    }

    // unit: entries are already root relative; see push_source in walk() for
    // why an absolute or ".."-escaping one is rejected rather than joined.
    for (const auto& value : all(doc, "unit")) {
        auto unit = parse_unit(value);
        if (!is_safe_relative_path(unit.path, unit.path)) {
            std::cerr << policy.tool << ": unsafe source path \"" << unit.path << "\"\n";
            return target;
        }
        target.sources.push_back(std::move(unit));
    }

    if (target.sources.empty()) {
        std::cerr << policy.tool << ": manifest declares no unit: entries\n";
        return target;
    }

    ok = true;
    return target;
}

bool order(const Tree& tree, std::vector<std::size_t>& out) {
    std::vector<int> state(tree.targets.size(), 0);
    out.clear();
    out.reserve(tree.targets.size());

    for (std::size_t i = 0; i < tree.targets.size(); ++i)
        if (!order_visit(i, tree, state, out)) return false;

    return true;
}

bool order_from(const Tree& tree, std::size_t index, std::vector<std::size_t>& out) {
    std::vector<int> state(tree.targets.size(), 0);
    out.clear();

    return order_visit(index, tree, state, out);
}

std::vector<std::filesystem::path> closure(const Tree& tree, std::size_t index) {
    std::vector<bool> seen(tree.targets.size(), false);
    std::vector<std::filesystem::path> objects;
    closure_visit(index, tree, seen, objects);
    return objects;
}

int run(const Toolchain& toolchain, const std::string& command) {
    if (toolchain.verbose) std::cout << "    " << command << "\n";

    // The child writes straight to the terminal; without this our own buffered
    // output would appear after it when stdout is a pipe.
    std::cout.flush();

    const int status = std::system(command.c_str());
    if (status == -1) return -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

// Single quotes disable every form of shell expansion, and the only character
// that cannot appear between them is the single quote itself, which is handled
// by closing the run, emitting an escaped quote and reopening. Double quotes
// would not do: $(), `` and $NAME all still expand inside them, so a path such
// as "$(touch x).cppm" would execute rather than name a file.
std::string shell_quote(const std::filesystem::path& path) {
    const std::string& text = path.native();

    std::string quoted;
    quoted.reserve(text.size() + 2);

    quoted += '\'';
    for (const char c : text) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';

    return quoted;
}

int compile(const Toolchain& toolchain, BuildableNode& target,
            const std::filesystem::path& build_dir,
            const std::vector<std::filesystem::path>& include_directories) {
    std::error_code ec;

    const auto bmi_dir = build_dir / "bmi";
    if (toolchain.family == CompilerFamily::Clang) {
        if (!within_root(bmi_dir)) {
            std::cerr << "build: refusing to write outside the project: " << bmi_dir.string()
                      << "\n";
            return exit_manifest;
        }
        std::filesystem::create_directories(bmi_dir, ec);
        if (ec) {
            std::cerr << "build: cannot create " << bmi_dir.string() << ": " << ec.message()
                      << "\n";
            return exit_compile;
        }
    }

    for (const auto& source : target.sources) {
        if (!safe_exists(source.path)) {
            std::cerr << "build: source does not exist: " << source.path << "\n";
            return exit_manifest;
        }

        const auto object = build_dir / (source.path + ".o");
        if (!within_root(object)) {
            std::cerr << "build: refusing to write outside the project: " << object.string() << "\n";
            return exit_manifest;
        }
        std::filesystem::create_directories(object.parent_path(), ec);
        if (ec) {
            std::cerr << "build: cannot create " << object.parent_path().string() << ": "
                      << ec.message() << "\n";
            return exit_compile;
        }

        std::cout << "    " << source.path << "\n";

        std::string command = toolchain.compiler.invocation + " " + toolchain.compiler.arguments;
        for (const auto& include : include_directories)
            command += " -I " + shell_quote(include);
        if (toolchain.family == CompilerFamily::Gcc) {
            command += " -fmodules-ts -x c++";
        } else {
            command += " -fprebuilt-module-path=" + shell_quote(bmi_dir);

            std::string module_name = source.module_name;
            if (module_name.empty() && target.kind == "module" &&
                std::filesystem::path(source.path).extension() == ".cppm")
                module_name = target.module_name;

            if (!module_name.empty()) {
                for (char& c : module_name)
                    if (c == ':') c = '-';
                const auto bmi = bmi_dir / (module_name + ".pcm");
                if (!within_root(bmi)) {
                    std::cerr << "build: refusing to write outside the project: " << bmi.string()
                              << "\n";
                    return exit_manifest;
                }
                command += " -fmodule-output=" + shell_quote(bmi);
            }
        }
        command += " -c " + shell_quote(source.path) + " -o " + shell_quote(object);
        if (run(toolchain, command) != 0) {
            std::cerr << "build: failed to compile " << source.path << "\n";
            return exit_compile;
        }

        target.objects.push_back(object);
    }

    return exit_ok;
}

int link(const Toolchain& toolchain,
         const std::vector<std::filesystem::path>& objects,
         const std::filesystem::path& output) {
    if (!within_root(output)) {
        std::cerr << "build: refusing to link outside the project: " << output.string() << "\n";
        return exit_link;
    }

    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        std::cerr << "build: cannot create " << output.parent_path().string() << ": "
                  << ec.message() << "\n";
        return exit_link;
    }

    auto temp = output;
    temp += ".link-tmp";
    std::filesystem::remove(temp, ec);

    std::string command = toolchain.linker.invocation + " " + toolchain.linker.arguments;
    for (const auto& object : objects) command += " " + shell_quote(object);
    command += " -o " + shell_quote(temp);

    if (run(toolchain, command) != 0) {
        std::cerr << "build: failed to link " << output.string() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    std::filesystem::rename(temp, output, ec);
    if (ec) {
        std::cerr << "build: failed to install " << output.string() << ": " << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    return exit_ok;
}

int install(const std::filesystem::path& from, const std::filesystem::path& bin_dir,
            const std::string& name) {
    if (!is_safe_name(name)) {
        std::cerr << "build: refusing to install to unsafe name: " << name << "\n";
        return exit_link;
    }

    const auto installed = bin_dir / name;
    if (!within_root(installed)) {
        std::cerr << "build: refusing to install outside the project: " << installed.string() << "\n";
        return exit_link;
    }

    std::error_code ec;
    std::filesystem::create_directories(bin_dir, ec);
    if (ec) {
        std::cerr << "build: cannot create " << bin_dir.string() << ": " << ec.message() << "\n";
        return exit_link;
    }

    // Written to a temporary file and renamed into place, rather than
    // removed and copied: rename() replaces the destination atomically, so
    // there is never a window where installed has just been deleted and not
    // yet replaced, and a failed copy never removes a working binary.
    const auto temp = bin_dir / (name + ".install-tmp");
    std::filesystem::remove(temp, ec);
    std::filesystem::copy_file(from, temp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "build: failed to install " << name << ": " << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    std::filesystem::rename(temp, installed, ec);
    if (ec) {
        std::cerr << "build: failed to install " << name << ": " << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return exit_link;
    }

    return exit_ok;
}

bool clear_module_cache(const std::filesystem::path& build_dir) {
    std::error_code ec;
    std::filesystem::remove_all("gcm.cache", ec);
    if (ec) {
        std::cerr << "build: cannot clear gcm.cache: " << ec.message() << "\n";
        return false;
    }

    const auto bmi_dir = build_dir / "bmi";
    if (!within_root(bmi_dir)) {
        std::cerr << "build: refusing to clear outside the project: " << bmi_dir.string() << "\n";
        return false;
    }
    std::filesystem::remove_all(bmi_dir, ec);
    if (ec) {
        std::cerr << "build: cannot clear " << bmi_dir.string() << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}

std::optional<std::string> cmake_bracket_argument(std::string_view value,
                                                  std::size_t max_equals) {
    if (value.find('\n') != std::string_view::npos || value.find('\r') != std::string_view::npos ||
        value.find(';') != std::string_view::npos)
        return std::nullopt;

    std::vector<std::size_t> order;
    order.push_back(2);
    for (std::size_t n = 0; n <= max_equals; ++n) {
        if (n != 2) order.push_back(n);
    }
    for (const auto n : order) {
        const std::string closing = "]" + std::string(n, '=') + "]";
        if (value.find(closing) == std::string_view::npos) {
            const std::string equals(n, '=');
            return "[" + equals + "[" + std::string(value) + "]" + equals + "]";
        }
    }
    return std::nullopt;
}

bool write_toolchain_cmake(const std::filesystem::path& destination,
                           const Toolchain& toolchain,
                           const Platform& platform) {
    if (toolchain.c_compiler.invocation.empty()) {
        std::cerr << "build: external build requires a configured C compiler; rerun configure\n";
        return false;
    }
    if (toolchain.compiler.invocation.empty()) {
        std::cerr << "build: external build requires a configured C++ compiler; rerun configure\n";
        return false;
    }

    const auto c_bracket = cmake_bracket_argument(toolchain.c_compiler.invocation);
    if (!c_bracket) {
        std::cerr << "build: invalid C compiler path for toolchain: "
                  << toolchain.c_compiler.invocation << "\n";
        return false;
    }
    const auto cxx_bracket = cmake_bracket_argument(toolchain.compiler.invocation);
    if (!cxx_bracket) {
        std::cerr << "build: invalid C++ compiler path for toolchain: "
                  << toolchain.compiler.invocation << "\n";
        return false;
    }

    std::string sysroot_bracket;
    if (platform.sysroot) {
        const auto bracket = cmake_bracket_argument(platform.sysroot->generic_string());
        if (!bracket) {
            std::cerr << "build: invalid sysroot path for toolchain: "
                      << platform.sysroot->generic_string() << "\n";
            return false;
        }
        sysroot_bracket = *bracket;
    }

    const std::string_view system_name =
        platform.system == mm::configure::PlatformSystem::BareMetal ? "Generic" : "Linux";

    std::ostringstream out;
    out << "set(CMAKE_SYSTEM_NAME " << system_name << ")\n";
    out << "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n";
    out << "set(CMAKE_C_COMPILER " << *c_bracket << ")\n";
    out << "set(CMAKE_CXX_COMPILER " << *cxx_bracket << ")\n";
    if (!sysroot_bracket.empty()) {
        out << "set(CMAKE_SYSROOT " << sysroot_bracket << ")\n";
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        std::cerr << "build: cannot create directory for " << destination.string() << ": "
                  << ec.message() << "\n";
        return false;
    }

    std::ofstream file(destination);
    if (!file.is_open()) {
        std::cerr << "build: cannot open " << destination.string() << " for writing\n";
        return false;
    }
    file << out.str();
    return file.good();
}

bool write_inputs_cmake(const std::filesystem::path& destination,
                        const std::vector<std::filesystem::path>& objects,
                        const std::string& output_name,
                        const std::filesystem::path& library_source,
                        const std::string& board_name) {
    std::string objects_text;
    for (const auto& obj : objects) {
        const auto bracket = cmake_bracket_argument(obj.generic_string());
        if (!bracket) {
            std::cerr << "build: invalid object path for external build: "
                      << obj.generic_string() << "\n";
            return false;
        }
        objects_text += "  " + *bracket + "\n";
    }

    const auto name_bracket = cmake_bracket_argument(output_name);
    if (!name_bracket) {
        std::cerr << "build: invalid output name for external build: "
                  << output_name << "\n";
        return false;
    }

    const auto source_bracket = cmake_bracket_argument(library_source.generic_string());
    if (!source_bracket) {
        std::cerr << "build: invalid library source path for external build: "
                  << library_source.generic_string() << "\n";
        return false;
    }

    const auto board_bracket = cmake_bracket_argument(board_name);
    if (!board_bracket) {
        std::cerr << "build: invalid board name for external build: "
                  << board_name << "\n";
        return false;
    }

    std::ostringstream out;
    out << "set(MM_OBJECTS\n" << objects_text << ")\n";
    out << "set(MM_OUTPUT_NAME " << *name_bracket << ")\n";
    out << "set(MM_LIBRARY_SOURCE " << *source_bracket << ")\n";
    out << "set(MM_BOARD " << *board_bracket << ")\n";

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        std::cerr << "build: cannot create directory for " << destination.string() << ": "
                  << ec.message() << "\n";
        return false;
    }

    std::ofstream file(destination);
    if (!file.is_open()) {
        std::cerr << "build: cannot open " << destination.string() << " for writing\n";
        return false;
    }
    file << out.str();
    return file.good();
}

const std::vector<ProjectionSchema>& projection_schemas() {
    static const std::vector<ProjectionSchema> schemas = {
        {"arm-none-eabi", {"-march=", "-mthumb", "-mfloat-abi=", "-mfpu=", "-mcmse"}},
        {"m68k-linux-gnu", {"-march=", "-mcpu=", "-m68881", "-mhard-float", "-msoft-float"}},
    };
    return schemas;
}

const ProjectionSchema* find_projection_schema(std::string_view target_triple) {
    for (const auto& s : projection_schemas()) {
        if (s.target_triple == target_triple) return &s;
    }
    return nullptr;
}

namespace {

class Fingerprint {
public:
    void add(std::string_view label, std::string_view value) {
        append(std::to_string(label.size()));
        append(":");
        append(label);
        append(":");
        append(std::to_string(value.size()));
        append(":");
        append(value);
        append("\n");
    }

    [[nodiscard]] std::string value() const {
        std::ostringstream out;
        out << std::hex << std::setfill('0') << std::setw(16) << value_;
        return out.str();
    }

private:
    void append(std::string_view value) {
        for (const unsigned char byte : value) {
            value_ ^= byte;
            value_ *= 1099511628211ULL;
        }
    }

    std::uint64_t value_ = 14695981039346656037ULL;
};

std::filesystem::path resolve_executable_path(std::string_view name) {
    if (name.find('/') != std::string_view::npos) {
        std::error_code ec;
        auto p = std::filesystem::canonical(name, ec);
        return ec ? std::filesystem::path(name) : p;
    }
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) return name;
    std::string_view path_view(path_env);
    while (!path_view.empty()) {
        const auto colon = path_view.find(':');
        const auto dir = colon == std::string_view::npos ? path_view : path_view.substr(0, colon);
        if (!dir.empty()) {
            auto candidate = std::filesystem::path(dir) / name;
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                auto p = std::filesystem::canonical(candidate, ec);
                return ec ? candidate : p;
            }
        }
        if (colon == std::string_view::npos) break;
        path_view = path_view.substr(colon + 1);
    }
    return name;
}

bool read_binary_file(const std::filesystem::path& path, std::string& contents,
                      std::string_view tool) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << tool << ": cannot open " << path.string() << " for fingerprinting\n";
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) {
        std::cerr << tool << ": cannot read " << path.string() << " for fingerprinting\n";
        return false;
    }
    return true;
}

bool fingerprint_file(Fingerprint& fingerprint, std::string_view label,
                      const std::filesystem::path& path, std::string_view tool) {
    std::string contents;
    if (!read_binary_file(path, contents, tool)) return false;
    fingerprint.add(label, contents);
    return true;
}

bool fingerprint_directory(Fingerprint& fingerprint, const std::filesystem::path& directory,
                           std::string_view tool) {
    std::error_code ec;
    std::vector<std::filesystem::path> entries;
    std::filesystem::recursive_directory_iterator iterator(directory, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        std::cerr << tool << ": cannot walk bridge directory " << directory.string()
                  << ": " << ec.message() << "\n";
        return false;
    }
    while (iterator != end) {
        entries.push_back(iterator->path());
        iterator.increment(ec);
        if (ec) {
            std::cerr << tool << ": cannot walk bridge directory " << directory.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
    }
    std::sort(entries.begin(), entries.end());

    for (const auto& entry : entries) {
        const auto relative = entry.lexically_relative(directory).generic_string();
        const auto status = std::filesystem::symlink_status(entry, ec);
        if (ec) {
            std::cerr << tool << ": cannot inspect bridge entry " << entry.string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        if (std::filesystem::is_symlink(status)) {
            const auto target = std::filesystem::read_symlink(entry, ec);
            if (ec) {
                std::cerr << tool << ": cannot read bridge symlink " << entry.string()
                          << ": " << ec.message() << "\n";
                return false;
            }
            fingerprint.add("bridge-symlink-path", relative);
            fingerprint.add("bridge-symlink-target", target.generic_string());
        } else if (std::filesystem::is_regular_file(status)) {
            fingerprint.add("bridge-file-path", relative);
            if (!fingerprint_file(fingerprint, "bridge-file-content", entry, tool)) return false;
        } else if (std::filesystem::is_directory(status)) {
            fingerprint.add("bridge-directory", relative);
        } else {
            fingerprint.add("bridge-other", relative);
        }
    }
    return true;
}

bool program_version(const std::filesystem::path& program, std::string& version,
                     std::string_view tool) {
    const std::string command = "LC_ALL=C " + shell_quote(program) + " --version";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        std::cerr << tool << ": failed to query version of " << program.string() << "\n";
        return false;
    }
    version.clear();
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) version += buffer;
    const int status = ::pclose(pipe);
    if (status != 0) {
        std::cerr << tool << ": version query failed for " << program.string()
                  << " with status " << status << "\n";
        return false;
    }
    return true;
}

bool external_cache_identity(const std::filesystem::path& bridge_dir,
                             const std::filesystem::path& toolchain_file,
                             const std::filesystem::path& inputs_file,
                             const Toolchain& toolchain,
                             const Platform& platform,
                             std::string_view board_name,
                             std::string_view output_name,
                             const std::map<std::string, std::string>& project_projection,
                             std::filesystem::path& cmake_program,
                             std::string& identity,
                             std::string_view tool) {
    Fingerprint fingerprint;
    fingerprint.add("identity-format", "1");
    fingerprint.add("generator", "Unix Makefiles");
    fingerprint.add("bridge-path", bridge_dir.generic_string());
    if (!fingerprint_directory(fingerprint, bridge_dir, tool) ||
        !fingerprint_file(fingerprint, "mm-toolchain.cmake", toolchain_file, tool) ||
        !fingerprint_file(fingerprint, "mm-inputs.cmake", inputs_file, tool))
        return false;

    cmake_program = resolve_executable_path("cmake");
    const auto c_program = resolve_executable_path(toolchain.c_compiler.invocation);
    const auto cxx_program = resolve_executable_path(toolchain.compiler.invocation);
    std::string cmake_version;
    std::string c_version;
    std::string cxx_version;
    if (!program_version(cmake_program, cmake_version, tool) ||
        !program_version(c_program, c_version, tool) ||
        !program_version(cxx_program, cxx_version, tool))
        return false;

    fingerprint.add("cmake-path", cmake_program.generic_string());
    fingerprint.add("cmake-version", cmake_version);
    fingerprint.add("c-driver-path", c_program.generic_string());
    fingerprint.add("c-driver-version", c_version);
    fingerprint.add("cxx-driver-path", cxx_program.generic_string());
    fingerprint.add("cxx-driver-version", cxx_version);
    fingerprint.add("c-options", toolchain.c_compiler.arguments);
    fingerprint.add("cxx-options", toolchain.compiler.arguments);
    fingerprint.add("link-options", toolchain.linker.arguments);
    fingerprint.add("target", platform.target);
    fingerprint.add("system", std::to_string(static_cast<int>(platform.system)));
    fingerprint.add("sysroot", platform.sysroot ? platform.sysroot->generic_string() : "");
    fingerprint.add("board", board_name);
    fingerprint.add("output", output_name);
    for (const auto& [field, value] : project_projection) {
        fingerprint.add("projection-field", field);
        fingerprint.add("projection-value", value);
    }
    identity = fingerprint.value();
    return true;
}

bool read_cache_identity(const std::filesystem::path& path, std::string& identity) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::getline(in, identity);
    return in.good() || in.eof();
}

bool write_cache_identity(const std::filesystem::path& path, std::string_view identity,
                          std::string_view tool) {
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << tool << ": cannot write external cache identity " << temporary.string()
                  << "\n";
        return false;
    }
    out << identity << "\n";
    out.close();
    if (!out) {
        std::cerr << tool << ": cannot write external cache identity " << temporary.string()
                  << "\n";
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        std::cerr << tool << ": cannot publish external cache identity " << path.string()
                  << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}

std::string read_json_string(std::string_view s, std::size_t& pos) {
    std::string result;
    if (pos >= s.size() || s[pos] != '"') return result;
    ++pos;
    while (pos < s.size()) {
        if (s[pos] == '"') {
            ++pos;
            return result;
        }
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            ++pos;
            char c = s[pos];
            if (c == '"' || c == '\\' || c == '/') result += c;
            else if (c == 'b') result += '\b';
            else if (c == 'f') result += '\f';
            else if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else if (c == 't') result += '\t';
            else result += c;
            ++pos;
        } else {
            result += s[pos];
            ++pos;
        }
    }
    return result;
}

void skip_json_whitespace(std::string_view s, std::size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
}

void skip_json_value(std::string_view s, std::size_t& pos) {
    skip_json_whitespace(s, pos);
    if (pos >= s.size()) return;
    if (s[pos] == '"') {
        read_json_string(s, pos);
    } else if (s[pos] == '[') {
        ++pos;
        while (pos < s.size() && s[pos] != ']') {
            skip_json_value(s, pos);
            skip_json_whitespace(s, pos);
            if (pos < s.size() && s[pos] == ',') ++pos;
        }
        if (pos < s.size() && s[pos] == ']') ++pos;
    } else if (s[pos] == '{') {
        ++pos;
        while (pos < s.size() && s[pos] != '}') {
            skip_json_value(s, pos);
            skip_json_whitespace(s, pos);
            if (pos < s.size() && s[pos] == ':') {
                ++pos;
                skip_json_value(s, pos);
            }
            skip_json_whitespace(s, pos);
            if (pos < s.size() && s[pos] == ',') ++pos;
        }
        if (pos < s.size() && s[pos] == '}') ++pos;
    } else {
        while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']' &&
               s[pos] != ' ' && s[pos] != '\t' && s[pos] != '\n' && s[pos] != '\r')
            ++pos;
    }
}

bool split_command_string(std::string_view command,
                          std::vector<std::string>& tokens,
                          std::string_view tool) {
    tokens.clear();
    std::size_t i = 0;
    while (i < command.size()) {
        while (i < command.size() && (command[i] == ' ' || command[i] == '\t' ||
                                      command[i] == '\n' || command[i] == '\r'))
            ++i;
        if (i >= command.size()) break;

        std::string token;
        bool in_quotes = false;
        while (i < command.size()) {
            if (command[i] == '\\' && i + 1 < command.size()) {
                token += command[i + 1];
                i += 2;
            } else if (command[i] == '"') {
                in_quotes = !in_quotes;
                ++i;
            } else if (!in_quotes && (command[i] == ' ' || command[i] == '\t' ||
                                      command[i] == '\n' || command[i] == '\r')) {
                break;
            } else {
                token += command[i];
                ++i;
            }
        }
        if (token.starts_with('@')) {
            std::cerr << tool << ": response file argument in compile command is rejected: "
                      << token << "\n";
            return false;
        }
        tokens.push_back(std::move(token));
    }
    return true;
}

bool read_abi_probe_path(const std::filesystem::path& file,
                         std::filesystem::path& probe_path,
                         std::string_view tool) {
    std::ifstream in(file);
    if (!in.is_open()) {
        std::cerr << tool << ": cannot read ABI probe file: " << file.string() << "\n";
        return false;
    }
    std::string line;
    if (!std::getline(in, line)) {
        std::cerr << tool << ": ABI probe file is empty: " << file.string() << "\n";
        return false;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' ||
                             line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
    line = line.substr(start);
    if (line.empty()) {
        std::cerr << tool << ": ABI probe path is empty in " << file.string() << "\n";
        return false;
    }
    std::error_code ec;
    probe_path = std::filesystem::canonical(line, ec);
    if (ec) {
        std::cerr << tool << ": cannot resolve ABI probe path: " << line << "\n";
        return false;
    }
    return true;
}

bool extract_probe_options(
    const std::filesystem::path& compile_commands_file,
    const std::filesystem::path& canonical_probe,
    std::string_view recorded_c_driver,
    std::vector<std::string>& sanitised_options,
    std::string_view tool) {
    std::ifstream in(compile_commands_file);
    if (!in.is_open()) {
        std::cerr << tool << ": cannot open " << compile_commands_file.string() << "\n";
        return false;
    }
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t pos = 0;
    skip_json_whitespace(json, pos);
    if (pos >= json.size() || json[pos] != '[') {
        std::cerr << tool << ": invalid compile database in " << compile_commands_file.string() << "\n";
        return false;
    }
    ++pos;

    std::size_t matches = 0;
    std::string matching_command;

    while (pos < json.size()) {
        skip_json_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') {
            ++pos;
            break;
        }
        if (pos < json.size() && json[pos] == ',') {
            ++pos;
            skip_json_whitespace(json, pos);
        }
        if (pos >= json.size() || json[pos] != '{') {
            std::cerr << tool << ": malformed compile database entry in "
                      << compile_commands_file.string() << "\n";
            return false;
        }
        ++pos;

        std::string directory;
        std::string command;
        std::string file;
        bool has_arguments = false;

        while (pos < json.size()) {
            skip_json_whitespace(json, pos);
            if (pos < json.size() && json[pos] == '}') {
                ++pos;
                break;
            }
            if (pos < json.size() && json[pos] == ',') {
                ++pos;
                skip_json_whitespace(json, pos);
            }
            if (pos >= json.size() || json[pos] != '"') {
                std::cerr << tool << ": malformed compile database key in "
                          << compile_commands_file.string() << "\n";
                return false;
            }
            std::string key = read_json_string(json, pos);
            skip_json_whitespace(json, pos);
            if (pos >= json.size() || json[pos] != ':') {
                std::cerr << tool << ": missing ':' in compile database in "
                          << compile_commands_file.string() << "\n";
                return false;
            }
            ++pos;
            skip_json_whitespace(json, pos);

            if (key == "arguments") {
                has_arguments = true;
                skip_json_value(json, pos);
            } else if (key == "command") {
                if (pos < json.size() && json[pos] == '"') {
                    command = read_json_string(json, pos);
                } else {
                    has_arguments = true;
                    skip_json_value(json, pos);
                }
            } else if (key == "file") {
                if (pos < json.size() && json[pos] == '"') {
                    file = read_json_string(json, pos);
                } else {
                    skip_json_value(json, pos);
                }
            } else if (key == "directory") {
                if (pos < json.size() && json[pos] == '"') {
                    directory = read_json_string(json, pos);
                } else {
                    skip_json_value(json, pos);
                }
            } else {
                skip_json_value(json, pos);
            }
        }

        if (has_arguments) {
            std::cerr << tool << ": compile_commands.json entry uses arguments array; only command string is supported\n";
            return false;
        }

        if (!file.empty()) {
            std::filesystem::path fp(file);
            if (fp.is_relative() && !directory.empty()) {
                fp = std::filesystem::path(directory) / fp;
            }
            std::error_code ec;
            auto can_fp = std::filesystem::canonical(fp, ec);
            if (!ec && can_fp == canonical_probe) {
                matches++;
                matching_command = command;
            }
        }
    }

    if (matches == 0) {
        std::cerr << tool << ": ABI probe " << canonical_probe.string()
                  << " matches no entry in compile database\n";
        return false;
    }
    if (matches > 1) {
        std::cerr << tool << ": ABI probe " << canonical_probe.string()
                  << " matches " << matches << " entries in compile database; expected exactly one\n";
        return false;
    }

    std::vector<std::string> tokens;
    if (!split_command_string(matching_command, tokens, tool)) return false;
    if (tokens.empty()) {
        std::cerr << tool << ": compile command for ABI probe is empty\n";
        return false;
    }

    const auto cmd_driver_canonical = resolve_executable_path(tokens[0]);
    const auto recorded_driver_canonical = resolve_executable_path(recorded_c_driver);
    if (cmd_driver_canonical != recorded_driver_canonical) {
        std::cerr << tool << ": external build C driver mismatch: command uses " << tokens[0]
                  << " (" << cmd_driver_canonical.string() << ") but configuration recorded "
                  << recorded_c_driver << " (" << recorded_driver_canonical.string() << ")\n";
        return false;
    }

    sanitised_options.clear();
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i].starts_with("-m")) {
            sanitised_options.push_back(tokens[i]);
        }
    }
    return true;
}

}  // namespace

bool query_driver_projection(
    const std::string& c_driver,
    const std::vector<std::string>& sanitised_options,
    const ProjectionSchema& schema,
    std::map<std::string, std::string>& projection,
    std::string_view tool) {
    std::string command = "LC_ALL=C " + shell_quote(c_driver);
    for (const auto& opt : sanitised_options) {
        command += " " + shell_quote(opt);
    }
    command += " -Q --help=target -c";

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        std::cerr << tool << ": failed to execute driver query: " << command << "\n";
        return false;
    }
    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    const int status = ::pclose(pipe);
    if (status != 0) {
        std::cerr << tool << ": driver query failed with status " << status << "\n";
        return false;
    }

    return parse_driver_projection(output, schema, projection, tool);
}

bool parse_driver_projection(
    std::string_view output,
    const ProjectionSchema& schema,
    std::map<std::string, std::string>& projection,
    std::string_view tool) {
    projection.clear();
    std::set<std::string> seen;
    std::set<std::string> schema_fields;
    for (const auto f : schema.fields) schema_fields.emplace(f);

    std::istringstream stream{std::string(output)};
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        std::size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        if (start >= line.size()) continue;
        std::string_view trimmed(&line[start], line.size() - start);

        if (!trimmed.starts_with("-m")) continue;

        const auto space_pos = trimmed.find_first_of(" \t");
        const auto opt_name = std::string(trimmed.substr(0, space_pos));
        if (!schema_fields.contains(opt_name)) continue;

        if (seen.contains(opt_name)) {
            std::cerr << tool << ": repeated ABI projection field in driver output: "
                      << opt_name << "\n";
            return false;
        }

        const auto val_start = space_pos == std::string_view::npos
                                   ? std::string_view::npos
                                   : trimmed.find_first_not_of(" \t", space_pos);
        const auto val = val_start == std::string_view::npos
                             ? std::string{}
                             : std::string(trimmed.substr(val_start));

        if (!opt_name.ends_with('=')) {
            if (val != "[enabled]" && val != "[disabled]") {
                std::cerr << tool << ": unrecognised value for boolean projection field "
                          << opt_name << ": \"" << val << "\"\n";
                return false;
            }
        }

        projection[opt_name] = val;
        seen.insert(opt_name);
    }

    for (const auto f : schema.fields) {
        if (!seen.contains(std::string(f))) {
            std::cerr << tool << ": driver query output missing required ABI projection field: "
                      << f << "\n";
            return false;
        }
    }
    return true;
}

bool publish_external_results(
    const std::filesystem::path& results_file,
    const std::filesystem::path& external_dir,
    const std::string& output_name,
    const std::filesystem::path& target_output,
    std::string_view tool) {
    std::ifstream in(results_file);
    if (!in.is_open()) {
        std::cerr << tool << ": cannot open " << results_file.string() << "\n";
        return false;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    while (!lines.empty()) {
        const auto& last = lines.back();
        if (last.find_first_not_of(" \t") == std::string::npos) {
            lines.pop_back();
        } else {
            break;
        }
    }
    if (lines.empty()) {
        std::cerr << tool << ": " << results_file.string() << " is empty\n";
        return false;
    }
    if (lines[0].empty() || lines[0].find_first_not_of(" \t") == std::string::npos) {
        std::cerr << tool << ": first line of " << results_file.string() << " is empty\n";
        return false;
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty() || lines[i].find_first_not_of(" \t") == std::string::npos) {
            std::cerr << tool << ": blank line in " << results_file.string() << " at line "
                      << (i + 1) << "\n";
            return false;
        }
    }

    std::error_code ec;
    const auto can_ext_dir = std::filesystem::canonical(external_dir, ec);
    if (ec) {
        std::cerr << tool << ": cannot resolve external build directory: "
                  << external_dir.string() << "\n";
        return false;
    }

    std::set<std::filesystem::path> seen_sources;
    std::map<std::filesystem::path, std::filesystem::path> publications;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::filesystem::path raw_path(lines[i]);
        if (!raw_path.is_absolute()) {
            std::cerr << tool << ": artifact path is not absolute: " << lines[i] << "\n";
            return false;
        }
        if (!std::filesystem::is_regular_file(raw_path, ec) || ec) {
            std::cerr << tool << ": artifact is not an existing regular file: " << lines[i] << "\n";
            return false;
        }
        auto can_src = std::filesystem::canonical(raw_path, ec);
        if (ec) {
            std::cerr << tool << ": cannot resolve artifact path: " << lines[i] << "\n";
            return false;
        }
        if (!path_within(can_ext_dir, can_src)) {
            std::cerr << tool << ": artifact resolves outside external build directory: "
                      << lines[i] << "\n";
            return false;
        }

        if (seen_sources.contains(can_src)) {
            std::cerr << tool << ": duplicate artifact source: " << lines[i] << "\n";
            return false;
        }
        seen_sources.insert(can_src);

        std::filesystem::path dest;
        if (i == 0) {
            dest = target_output;
        } else {
            const std::string filename = raw_path.filename().string();
            if (!filename.starts_with(output_name)) {
                std::cerr << tool << ": supplemental artifact does not begin with output name \""
                          << output_name << "\": " << lines[i] << "\n";
                return false;
            }
            const std::string suffix = filename.substr(output_name.length());
            if (suffix.empty() || suffix[0] != '.') {
                std::cerr << tool << ": supplemental artifact suffix must start with a dot: "
                          << lines[i] << "\n";
                return false;
            }
            dest = target_output.parent_path() / (target_output.filename().string() + suffix);
        }

        if (publications.contains(dest)) {
            std::cerr << tool << ": colliding artifact destination: " << dest.string() << "\n";
            return false;
        }
        publications[dest] = raw_path;
    }

    for (const auto& [dest, src] : publications) {
        if (!std::filesystem::is_regular_file(src, ec) || ec) {
            std::cerr << tool << ": declared artifact missing after build: " << src.string() << "\n";
            return false;
        }
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) {
            std::cerr << tool << ": cannot create destination directory " << dest.parent_path().string()
                      << ": " << ec.message() << "\n";
            return false;
        }
        const auto temp = dest.string() + ".publish-tmp";
        std::filesystem::remove(temp, ec);
        std::filesystem::copy_file(src, temp, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << tool << ": failed to copy artifact " << src.string() << " to " << temp
                      << ": " << ec.message() << "\n";
            return false;
        }
        std::filesystem::rename(temp, dest, ec);
        if (ec) {
            std::cerr << tool << ": failed to publish artifact " << dest.string()
                      << ": " << ec.message() << "\n";
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    return true;
}

int external_link(
    const Project& project,
    const Platform& platform,
    const Toolchain& toolchain,
    const std::string& app_name,
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& build_dir,
    const std::filesystem::path& target_output,
    bool verbose) {
    if (!platform.sdk) {
        std::cerr << "build: external link requires a selected SDK\n";
        return exit_manifest;
    }
    const SdkDefinition* sdk = nullptr;
    for (const auto& entry : project.sdks) {
        if (entry.name == *platform.sdk) {
            sdk = &entry;
            break;
        }
    }
    if (sdk == nullptr || sdk->library.empty()) {
        std::cerr << "build: selected SDK \"" << *platform.sdk << "\" names no library\n";
        return exit_manifest;
    }
    const LibraryDefinition* library = nullptr;
    for (const auto& lib : project.libraries) {
        if (lib.name == sdk->library) {
            library = &lib;
            break;
        }
    }
    if (library == nullptr) {
        std::cerr << "build: SDK library \"" << sdk->library << "\" not found\n";
        return exit_manifest;
    }
    if (library->external_build != "cmake") {
        std::cerr << "build: unsupported external-build: " << library->external_build << "\n";
        return exit_manifest;
    }
    if (toolchain.c_compiler.invocation.empty()) {
        std::cerr << "build: external build requires a configured C compiler; rerun configure\n";
        return exit_compile;
    }

    const std::string board_name = (platform.board && !platform.board->empty()) ? *platform.board : "none";
    const auto external_dir = std::filesystem::absolute(build_dir / "external" / library->name / board_name / app_name);

    std::error_code ec;
    std::filesystem::create_directories(external_dir, ec);
    if (ec) {
        std::cerr << "build: cannot create external build directory " << external_dir.string()
                  << ": " << ec.message() << "\n";
        return exit_link;
    }

    const auto toolchain_file = external_dir / "mm-toolchain.cmake";
    if (!write_toolchain_cmake(toolchain_file, toolchain, platform)) {
        return exit_compile;
    }

    std::vector<std::filesystem::path> abs_objects;
    for (const auto& obj : objects) {
        abs_objects.push_back(std::filesystem::absolute(obj));
    }
    const auto abs_lib_source = std::filesystem::absolute(library->source);
    const auto inputs_file = external_dir / "mm-inputs.cmake";
    const std::string bridge_board = (platform.board && !platform.board->empty()) ? *platform.board : "";
    if (!write_inputs_cmake(inputs_file, abs_objects, app_name, abs_lib_source, bridge_board)) {
        return exit_manifest;
    }

    const auto bridge_dir = std::filesystem::absolute(library->manifest.parent_path() / "cmake");

    std::vector<std::string> project_options;
    std::vector<std::string> project_tokens;
    if (!split_command_string(toolchain.compiler.arguments, project_tokens, "build")) {
        return exit_link;
    }
    for (const auto& tok : project_tokens) {
        if (tok.starts_with("-m")) project_options.push_back(tok);
    }

    const auto* schema = find_projection_schema(platform.target);
    if (schema == nullptr) {
        std::cerr << "build: target \"" << platform.target << "\" has no ABI projection schema\n";
        return exit_manifest;
    }

    std::map<std::string, std::string> project_proj;
    if (!query_driver_projection(toolchain.c_compiler.invocation, project_options, *schema,
                                 project_proj, "build")) {
        return exit_link;
    }

    std::filesystem::path cmake_program;
    std::string cache_identity;
    if (!external_cache_identity(bridge_dir, toolchain_file, inputs_file, toolchain, platform,
                                 bridge_board, app_name, project_proj, cmake_program,
                                 cache_identity, "build")) {
        return exit_link;
    }

    const auto identity_file = external_dir / "mm-cache-identity.txt";
    std::string previous_identity;
    const bool has_identity = read_cache_identity(identity_file, previous_identity);
    const bool has_cmake_cache = std::filesystem::exists(external_dir / "CMakeCache.txt", ec);
    if (ec) {
        std::cerr << "build: cannot inspect external build cache " << external_dir.string()
                  << ": " << ec.message() << "\n";
        return exit_link;
    }
    if (has_cmake_cache && (!has_identity || previous_identity != cache_identity)) {
        if (!within_root(external_dir)) {
            std::cerr << "build: refusing to clear external cache outside the project: "
                      << external_dir.string() << "\n";
            return exit_link;
        }
        if (verbose) {
            std::cout << "    external cache identity "
                      << (has_identity ? "changed" : "missing") << "; clearing "
                      << external_dir.string() << "\n";
        }
        std::filesystem::remove_all(external_dir, ec);
        if (ec) {
            std::cerr << "build: cannot clear external build cache " << external_dir.string()
                      << ": " << ec.message() << "\n";
            return exit_link;
        }
        std::filesystem::create_directories(external_dir, ec);
        if (ec) {
            std::cerr << "build: cannot recreate external build directory "
                      << external_dir.string() << ": " << ec.message() << "\n";
            return exit_link;
        }
        if (!write_toolchain_cmake(toolchain_file, toolchain, platform)) return exit_compile;
        if (!write_inputs_cmake(inputs_file, abs_objects, app_name, abs_lib_source, bridge_board))
            return exit_manifest;
    }

    std::string configure_cmd = shell_quote(cmake_program) +
                                " -G \"Unix Makefiles\" -DCMAKE_TOOLCHAIN_FILE=" +
                                shell_quote(toolchain_file) +
                                " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON -S " +
                                shell_quote(bridge_dir) + " -B " + shell_quote(external_dir);
    if (!verbose) configure_cmd += " >/dev/null 2>&1";

    if (run(toolchain, configure_cmd) != 0) {
        std::filesystem::remove(identity_file, ec);
        std::cerr << "build: external build configuration failed for " << app_name << "\n";
        return exit_link;
    }

    if (!write_cache_identity(identity_file, cache_identity, "build")) return exit_link;

    // Step 5: ABI probe and projection comparison
    const auto probe_txt = external_dir / "mm-abi-probe.txt";
    std::filesystem::path canonical_probe;
    if (!read_abi_probe_path(probe_txt, canonical_probe, "build")) {
        return exit_link;
    }

    const auto compile_commands_file = external_dir / "compile_commands.json";
    std::vector<std::string> bridge_sanitised_options;
    if (!extract_probe_options(compile_commands_file, canonical_probe,
                               toolchain.c_compiler.invocation, bridge_sanitised_options,
                               "build")) {
        return exit_link;
    }

    std::map<std::string, std::string> bridge_proj;
    if (!query_driver_projection(toolchain.c_compiler.invocation, bridge_sanitised_options,
                                 *schema, bridge_proj, "build")) {
        return exit_link;
    }

    bool projections_match = true;
    for (const auto f : schema->fields) {
        if (project_proj[std::string(f)] != bridge_proj[std::string(f)]) {
            projections_match = false;
            break;
        }
    }

    if (!projections_match) {
        std::cerr << "build: ABI projection mismatch between project and external build\n";
        std::cerr << "  project options: ";
        for (std::size_t i = 0; i < project_options.size(); ++i) {
            if (i > 0) std::cerr << " ";
            std::cerr << project_options[i];
        }
        std::cerr << "\n  bridge options:  ";
        for (std::size_t i = 0; i < bridge_sanitised_options.size(); ++i) {
            if (i > 0) std::cerr << " ";
            std::cerr << bridge_sanitised_options[i];
        }
        std::cerr << "\n  project projection:\n";
        for (const auto f : schema->fields) {
            std::cerr << "    " << f << " " << project_proj[std::string(f)] << "\n";
        }
        std::cerr << "  bridge projection:\n";
        for (const auto f : schema->fields) {
            std::cerr << "    " << f << " " << bridge_proj[std::string(f)] << "\n";
        }
        return exit_link;
    }

    // Step 6: Build target mm_external
    std::string build_cmd = shell_quote(cmake_program) + " --build " + shell_quote(external_dir) +
                            " --target mm_external";
    if (!verbose) build_cmd += " >/dev/null 2>&1";

    if (run(toolchain, build_cmd) != 0) {
        std::cerr << "build: external build failed for " << app_name << "\n";
        return exit_link;
    }

    // Step 7: Publish results
    const auto result_txt = external_dir / "mm-result.txt";
    if (!publish_external_results(result_txt, external_dir, app_name, target_output, "build")) {
        return exit_link;
    }

    return exit_ok;
}

}
