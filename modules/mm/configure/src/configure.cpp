// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

module mm.configure;

namespace mm::configure {

namespace {

bool valid_scalar(std::string_view value, bool allow_empty = false) {
    return (allow_empty || !value.empty()) && value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos;
}

bool valid_compiler(const CompilerSettings& compiler) {
    return valid_scalar(compiler.invocation) && valid_scalar(compiler.target) &&
           valid_scalar(compiler.platform) && valid_scalar(compiler.compile_flags) &&
           valid_scalar(compiler.link_flags);
}

bool valid_build_directory(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;

    const auto normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& component : normalized)
        if (component == "..") return false;

    return valid_scalar(normalized.generic_string());
}

bool valid_settings(const Settings& settings) {
    if (!valid_scalar(settings.name) || !valid_compiler(settings.host) ||
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

void write_compiler(std::ofstream& out, std::string_view prefix,
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

    const auto output_directory = project_root / "out";
    std::filesystem::create_directories(output_directory, ec);
    if (ec || !std::filesystem::is_directory(output_directory, ec) || ec) return false;

    const auto destination = output_directory / "config.mdy";
    const auto temporary = output_directory / "config.mdy.tmp";

    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    out << "---\n";
    out << "mm: 1.0\n";
    out << "kind: configuration\n";
    out << "name: " << settings.name << '\n';
    out << "target-compiler: "
        << (settings.target_compiler == CompilerSelection::Host ? "host" : "cross") << '\n';
    write_compiler(out, "host", settings.host);
    if (settings.cross) write_compiler(out, "cross", *settings.cross);
    out << "host-build-directory: " << settings.host_build_directory.generic_string() << '\n';
    out << "target-build-directory: " << settings.target_build_directory.generic_string() << '\n';
    out << "---\n";
    out.close();

    if (!out) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::error_code remove_ec;
        std::filesystem::remove(temporary, remove_ec);
        return false;
    }

    return true;
}

}  // namespace mm::configure
