// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <charconv>
#include <chrono>
#include <cstdlib>
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

bool valid_compiler(const CompilerSettings& compiler) {
    return valid_scalar(compiler.invocation) && valid_scalar(compiler.target) &&
           valid_scalar(compiler.platform) && valid_scalar(compiler.compile_flags) &&
           valid_scalar(compiler.link_flags);
}

bool valid_settings(const Settings& settings) {
    if (!valid_scalar(settings.name) || !valid_scalar(build_name(settings.build)) ||
        !valid_compiler(settings.host) ||
        !valid_build_directory(settings.host_build_directory) ||
        !valid_build_directory(settings.target_build_directory))
        return false;

    if (settings.cross && !valid_compiler(*settings.cross)) return false;
    if (settings.target_compiler == CompilerSelection::Cross) {
        if (!settings.cross) return false;
        if (settings.host_build_directory.lexically_normal() ==
            settings.target_build_directory.lexically_normal())
            return false;
    }

    return true;
}

void write_compiler(std::ostream& out, std::string_view prefix,
                    const CompilerSettings& compiler) {
    out << prefix << "-compiler-family: " << compiler_family_name(compiler.family) << '\n';
    out << prefix << "-compiler: " << compiler.invocation << '\n';
    out << prefix << "-target: " << compiler.target << '\n';
    out << prefix << "-platform: " << compiler.platform << '\n';
    out << prefix << "-compile-flags: " << compiler.compile_flags << '\n';
    out << prefix << "-link-flags: " << compiler.link_flags << '\n';
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
        return std::nullopt;
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

std::optional<std::string> get(std::string_view name) {
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

bool set(std::string_view name, std::string_view value, bool overwrite) {
    const std::string key(name);
    const std::string val(value);
    return ::setenv(key.c_str(), val.c_str(), overwrite ? 1 : 0) == 0;
}

bool unset(std::string_view name) {
    const std::string key(name);
    return ::unsetenv(key.c_str()) == 0;
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
    out << "mm: 1.0\n";
    out << "kind: configuration\n";
    out << "name: " << settings.name << '\n';
    out << "build: " << build_name(settings.build) << '\n';
    out << "target-compiler: "
        << (settings.target_compiler == CompilerSelection::Host ? "host" : "cross") << '\n';
    write_compiler(out, "host", settings.host);
    if (settings.cross) write_compiler(out, "cross", *settings.cross);
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

const std::vector<OptionSpec> registry = {
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
};

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
            << "\nresolved-by: configure\napplied-by-build: no\npath-base: project-root\n";
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
    std::cout << "Manifest options and locks recorded only; not applied by build or test yet\n";
    return true;
}

}  // namespace mm::configure
