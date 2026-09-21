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

module mm.build;

import mm.configure;
import mm.json;
import mm.mdy;
import :detail;
import :config;
import :manifest;
import :compile;
import :graph;
import :external;

// POSIX pipe declarations are hidden by newlib's strict C++ feature profile.
// Version and ABI probes run only in the host build tool, while the module's
// remaining interfaces stay compilable for target lanes.
extern "C" std::FILE* popen(const char*, const char*);
extern "C" int pclose(std::FILE*);

namespace mm::build {
// This unit implements the mm.build:external partition: the external CMake lane.
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
                        const std::string& board_name,
                        const std::vector<std::string>& board_chain,
                        const std::filesystem::path& toolchain_file,
                        const std::filesystem::path& c_compiler,
                        const std::filesystem::path& cxx_compiler) {
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

    std::string chain_text;
    for (const auto& entry : board_chain) {
        const auto bracket = cmake_bracket_argument(entry);
        if (!bracket) {
            std::cerr << "build: invalid board chain entry for external build: "
                      << entry << "\n";
            return false;
        }
        chain_text += "  " + *bracket + "\n";
    }

    const auto toolchain_bracket = cmake_bracket_argument(toolchain_file.generic_string());
    if (!toolchain_bracket) {
        std::cerr << "build: invalid toolchain path for external build: "
                  << toolchain_file.generic_string() << "\n";
        return false;
    }
    const auto c_bracket = cmake_bracket_argument(c_compiler.generic_string());
    if (!c_bracket) {
        std::cerr << "build: invalid C compiler path for external build: "
                  << c_compiler.generic_string() << "\n";
        return false;
    }
    const auto cxx_bracket = cmake_bracket_argument(cxx_compiler.generic_string());
    if (!cxx_bracket) {
        std::cerr << "build: invalid C++ compiler path for external build: "
                  << cxx_compiler.generic_string() << "\n";
        return false;
    }

    std::ostringstream out;
    out << "set(MM_OBJECTS\n" << objects_text << ")\n";
    out << "set(MM_OUTPUT_NAME " << *name_bracket << ")\n";
    out << "set(MM_LIBRARY_SOURCE " << *source_bracket << ")\n";
    out << "set(MM_BOARD " << *board_bracket << ")\n";
    if (board_chain.empty()) {
        out << "set(MM_BOARD_CHAIN)\n";
    } else {
        out << "set(MM_BOARD_CHAIN\n" << chain_text << ")\n";
    }
    out << "set(MM_TOOLCHAIN_FILE " << *toolchain_bracket << ")\n";
    out << "set(MM_C_COMPILER " << *c_bracket << ")\n";
    out << "set(MM_CXX_COMPILER " << *cxx_bracket << ")\n";

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

bool read_cmake_package_requirements(
    const std::filesystem::path& bridge_directory,
    std::vector<CMakePackageRequirement>& requirements,
    std::string_view tool) {
    requirements.clear();
    const auto requirements_file = bridge_directory / "mm-requires.txt";
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(requirements_file, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        return true;
    }
    if (ec) {
        std::cerr << tool << ": cannot inspect external bridge requirements "
                  << requirements_file.string() << ": " << ec.message() << "\n";
        return false;
    }
    if (!std::filesystem::is_regular_file(status)) {
        std::cerr << tool << ": external bridge requirements are not a regular file: "
                  << requirements_file.string() << "\n";
        return false;
    }

    std::ifstream input(requirements_file);
    if (!input.is_open()) {
        std::cerr << tool << ": cannot open external bridge requirements: "
                  << requirements_file.string() << "\n";
        return false;
    }

    std::set<std::string> seen;
    std::string variable;
    std::size_t line_number = 0;
    while (std::getline(input, variable)) {
        ++line_number;
        if (!variable.empty() && variable.back() == '\r') variable.pop_back();
        const auto valid_first = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        };
        const auto valid_rest = [&](char c) {
            return valid_first(c) || (c >= '0' && c <= '9');
        };
        const bool valid_name = variable.size() > 4 && variable.ends_with("_DIR") &&
                                valid_first(variable.front()) &&
                                std::all_of(variable.begin() + 1, variable.end(), valid_rest);
        if (!valid_name) {
            std::cerr << tool << ": invalid CMake package variable in "
                      << requirements_file.string() << " at line " << line_number
                      << ": " << variable << "\n";
            return false;
        }
        if (!seen.insert(variable).second) {
            std::cerr << tool << ": duplicate CMake package variable in "
                      << requirements_file.string() << " at line " << line_number
                      << ": " << variable << "\n";
            return false;
        }

        const std::string package = variable.substr(0, variable.size() - 4);
        const std::string config_name = package + "Config.cmake";
        const std::string lower_config_name = package + "-config.cmake";
        const char* configured = std::getenv(variable.c_str());
        if (configured == nullptr || *configured == '\0') {
            std::cerr << tool << ": external bridge requires CMake package variable "
                      << variable << "; it is unset (expected " << config_name
                      << " or " << lower_config_name << ")\n";
            return false;
        }

        const auto absolute =
            std::filesystem::absolute(std::filesystem::path(configured), ec).lexically_normal();
        if (ec || !std::filesystem::is_directory(absolute, ec) || ec) {
            std::cerr << tool << ": CMake package variable " << variable << " names "
                      << absolute.string() << ", which is not an existing directory (expected "
                      << config_name << " or " << lower_config_name << ")\n";
            return false;
        }

        ec.clear();
        const bool has_config = std::filesystem::is_regular_file(absolute / config_name, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) {
            std::cerr << tool << ": cannot inspect CMake package directory "
                      << absolute.string() << ": " << ec.message() << "\n";
            return false;
        }
        ec.clear();
        const bool has_lower_config =
            std::filesystem::is_regular_file(absolute / lower_config_name, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) {
            std::cerr << tool << ": cannot inspect CMake package directory "
                      << absolute.string() << ": " << ec.message() << "\n";
            return false;
        }
        if (!has_config && !has_lower_config) {
            std::cerr << tool << ": CMake package variable " << variable << " names "
                      << absolute.string() << ", which contains neither " << config_name
                      << " nor " << lower_config_name << "\n";
            return false;
        }

        auto canonical = std::filesystem::canonical(absolute, ec);
        if (ec) {
            std::cerr << tool << ": cannot resolve CMake package variable " << variable
                      << " at " << absolute.string() << ": " << ec.message() << "\n";
            return false;
        }
        requirements.push_back({variable, std::move(canonical)});
    }
    if (!input.eof()) {
        std::cerr << tool << ": cannot read external bridge requirements: "
                  << requirements_file.string() << "\n";
        return false;
    }
    return true;
}

const std::vector<ProjectionSchema>& projection_schemas() {
    static const std::vector<ProjectionSchema> schemas = {
        {"arm-none-eabi", {"-march=", "-mthumb", "-mfloat-abi=", "-mfpu=", "-mcmse"}},
        {"m68k-linux-gnu", {"-march=", "-mcpu=", "-m68881", "-mhard-float", "-msoft-float"}},
        // -march= and -mabi= are the pair that decides a RISC-V ABI. -mcpu= is
        // excluded for the reason the ARM measurement established: naming an
        // architecture leaves it empty, and the SDK may name a CPU where the
        // project names an architecture. -mstrict-align and -mcmodel= are code
        // generation rather than calling convention. Unconfirmed against a real
        // riscv32-pico-elf driver; see docs/modules-platforms.mdy.
        {"riscv32-pico-elf", {"-march=", "-mabi="}},
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
                           std::string_view label, std::string_view tool) {
    std::error_code ec;
    std::vector<std::filesystem::path> entries;
    std::filesystem::recursive_directory_iterator iterator(directory, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        std::cerr << tool << ": cannot walk fingerprinted directory " << directory.string()
                  << ": " << ec.message() << "\n";
        return false;
    }
    while (iterator != end) {
        entries.push_back(iterator->path());
        iterator.increment(ec);
        if (ec) {
            std::cerr << tool << ": cannot walk fingerprinted directory " << directory.string()
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
            fingerprint.add(std::string(label) + "-symlink-path", relative);
            fingerprint.add(std::string(label) + "-symlink-target", target.generic_string());
            if (std::filesystem::is_regular_file(entry, ec) && !ec &&
                !fingerprint_file(fingerprint, std::string(label) + "-symlink-content",
                                  entry, tool))
                return false;
        } else if (std::filesystem::is_regular_file(status)) {
            fingerprint.add(std::string(label) + "-file-path", relative);
            if (!fingerprint_file(fingerprint, std::string(label) + "-file-content", entry, tool))
                return false;
        } else if (std::filesystem::is_directory(status)) {
            fingerprint.add(std::string(label) + "-directory", relative);
        } else {
            fingerprint.add(std::string(label) + "-other", relative);
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
                             const std::vector<CMakePackageRequirement>& package_requirements,
                             std::filesystem::path& cmake_program,
                             std::string& identity,
                             std::string_view tool) {
    Fingerprint fingerprint;
    // Format 4 makes bridge-owned toolchain selection and the generic package
    // requirements protocol distinct from caches made by earlier contracts.
    fingerprint.add("identity-format", "4");
    fingerprint.add("generator", "Unix Makefiles");
    fingerprint.add("bridge-path", bridge_dir.generic_string());
    if (!fingerprint_directory(fingerprint, bridge_dir, "bridge", tool) ||
        !fingerprint_file(fingerprint, "mm-toolchain.cmake", toolchain_file, tool) ||
        !fingerprint_file(fingerprint, "mm-inputs.cmake", inputs_file, tool))
        return false;
    for (const auto& requirement : package_requirements) {
        fingerprint.add("cmake-package-variable", requirement.variable);
        fingerprint.add("cmake-package-path", requirement.directory.generic_string());
        if (!fingerprint_directory(fingerprint, requirement.directory,
                                   "cmake-package-" + requirement.variable, tool))
            return false;
    }

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

}  // namespace

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

    // The compile database through mm.json: a fault is reported with its
    // line and column rather than read past, and \u escapes in a path are
    // decoded rather than copied through as text.
    mm::json::Value database;
    const auto parsed = mm::json::parse(json, database);
    if (parsed.status != mm::json::Status::Ok) {
        std::cerr << tool << ": invalid compile database " << compile_commands_file.string()
                  << ":" << parsed.issue.line << ":" << parsed.issue.column << ": "
                  << parsed.issue.description << "\n";
        return false;
    }
    if (database.type() != mm::json::Type::Array) {
        std::cerr << tool << ": invalid compile database in " << compile_commands_file.string()
                  << ": the top level is not an array\n";
        return false;
    }

    std::size_t matches = 0;
    std::string matching_command;

    for (const auto& entry : database.items()) {
        if (entry.type() != mm::json::Type::Object) {
            std::cerr << tool << ": malformed compile database entry in "
                      << compile_commands_file.string() << "\n";
            return false;
        }
        const auto* arguments = entry.find("arguments");
        const auto* command = entry.find("command");
        const auto* file = entry.find("file");
        const auto* directory = entry.find("directory");
        if (arguments != nullptr ||
            (command != nullptr && command->type() != mm::json::Type::String)) {
            std::cerr << tool << ": compile_commands.json entry uses arguments array; only command string is supported\n";
            return false;
        }
        if (file == nullptr || file->type() != mm::json::Type::String) continue;
        std::filesystem::path fp(file->string());
        if (fp.is_relative() && directory != nullptr &&
            directory->type() == mm::json::Type::String)
            fp = std::filesystem::path(directory->string()) / fp;
        std::error_code ec;
        auto can_fp = std::filesystem::canonical(fp, ec);
        if (!ec && can_fp == canonical_probe) {
            matches++;
            if (command != nullptr) matching_command = std::string(command->string());
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
    // No -c: the driver answers --help=target by compiling a synthetic input
    // named help-dummy, and with -c it would also assemble it, leaving a
    // help-dummy.o in the working directory. The listing is the same.
    command += " -Q --help=target";

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
    const ArtifactContext& context,
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
        if (!context.check_artifact_path(dest) ||
            !path_contained_in(target_output.parent_path(), dest)) {
            std::cerr << tool << ": refusing to publish outside the project: "
                      << dest.string() << "\n";
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

bool publish_external_results(
    const std::filesystem::path& results_file,
    const std::filesystem::path& external_dir,
    const std::string& output_name,
    const std::filesystem::path& target_output,
    std::string_view tool) {
    ArtifactContext context({}, target_output.parent_path());
    return publish_external_results(results_file, external_dir, output_name,
                                    target_output, context, tool);
}

int external_link(
    const Project& project,
    const Platform& platform,
    const Toolchain& toolchain,
    const std::string& app_name,
    const std::vector<std::filesystem::path>& objects,
    const ArtifactContext& context,
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

    const auto bridge_dir = std::filesystem::absolute(library->manifest.parent_path() / "cmake");
    std::vector<CMakePackageRequirement> package_requirements;
    if (!read_cmake_package_requirements(bridge_dir, package_requirements, "build")) {
        return exit_manifest;
    }

    const auto c_driver = resolve_executable_path(toolchain.c_compiler.invocation);
    const auto cxx_driver = resolve_executable_path(toolchain.compiler.invocation);
    std::error_code driver_ec;
    const bool c_exists = c_driver.is_absolute() &&
                          std::filesystem::is_regular_file(c_driver, driver_ec) && !driver_ec;
    driver_ec.clear();
    const bool cxx_exists = cxx_driver.is_absolute() &&
                            std::filesystem::is_regular_file(cxx_driver, driver_ec) && !driver_ec;
    if (!c_exists || !cxx_exists) {
        std::cerr << "build: cannot resolve configured external-build C/C++ compilers: "
                  << toolchain.c_compiler.invocation << " and "
                  << toolchain.compiler.invocation << "\n";
        return exit_compile;
    }

    const std::string board_name = (platform.board && !platform.board->empty()) ? *platform.board : "none";
    const auto external_dir = std::filesystem::absolute(
        context.bridge_dir(library->name, board_name, app_name));

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
    std::vector<std::string> board_chain;
    if (platform.board && !platform.board->empty()) {
        board_chain.push_back(*platform.board);
        for (const auto& base : platform.board_derives_from) {
            board_chain.push_back(base);
        }
    }
    if (!write_inputs_cmake(inputs_file, abs_objects, app_name, abs_lib_source, bridge_board,
                            board_chain, toolchain_file, c_driver, cxx_driver)) {
        return exit_manifest;
    }

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
                                 bridge_board, app_name, project_proj, package_requirements,
                                 cmake_program, cache_identity, "build")) {
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
        if (!context.check_artifact_path(external_dir)) {
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
        if (!write_inputs_cmake(inputs_file, abs_objects, app_name, abs_lib_source, bridge_board,
                                board_chain, toolchain_file, c_driver, cxx_driver))
            return exit_manifest;
    }

    std::string configure_cmd = shell_quote(cmake_program) + " -G \"Unix Makefiles\"";
    configure_cmd += " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "
                     "-DFETCHCONTENT_FULLY_DISCONNECTED=ON";
    for (const auto& requirement : package_requirements) {
        configure_cmd += " " + shell_quote(std::filesystem::path(
            "-D" + requirement.variable + ":PATH=" + requirement.directory.generic_string()));
    }
    configure_cmd += " -S " + shell_quote(bridge_dir) + " -B " + shell_quote(external_dir);
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
    if (!publish_external_results(result_txt, external_dir, app_name,
                                  target_output, context, "build")) {
        return exit_link;
    }

    return exit_ok;
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
    ArtifactContext context(".", build_dir);
    return external_link(project, platform, toolchain, app_name, objects,
                         context, target_output, verbose);
}
}
