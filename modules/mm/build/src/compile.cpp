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

import mm.json;
import mm.mdy;

// POSIX pipe declarations are hidden by newlib's strict C++ feature profile.
// Version and ABI probes run only in the host build tool, while the module's
// remaining interfaces stay compilable for target lanes.
extern "C" std::FILE* popen(const char*, const char*);
extern "C" int pclose(std::FILE*);

namespace mm::build {
// This unit holds the mm.build implementation for: ile.
bool within_destination(const std::filesystem::path& destination,
                        const std::filesystem::path& path,
                        const std::filesystem::path& tree_root) {
    if (!path_contained_in(destination, path)) return false;
    if (!tree_root.empty() && !path_contained_in(tree_root, path)) return false;
    return true;
}
bool apple_host() {
    std::error_code ec;
    return std::filesystem::is_directory("/System/Library", ec) && !ec;
}

bool uses_clang_modules(const Toolchain& toolchain) {
    if (toolchain.family == CompilerFamily::Clang) return true;
    return apple_host() && toolchain.compiler.invocation.find("g++") != std::string::npos;
}

ArtifactContext::ArtifactContext(std::filesystem::path tree_root,
                                 std::filesystem::path output_root,
                                 std::filesystem::path tools_dir,
                                 bool external)
    : tree_root_(std::move(tree_root)),
      output_root_(std::move(output_root)),
      tools_dir_(std::move(tools_dir)),
      external_(external) {
    if (output_root_.empty()) {
        valid_ = false;
    } else if (tree_root_.empty()) {
        valid_ = true;
    } else {
        valid_ = path_contained_in(tree_root_, output_root_);
    }
}

std::string ArtifactContext::prefix(bool node_external) const {
    if (!external_) return "";
    return node_external ? "" : "graft/project/";
}

std::string ArtifactContext::prefix(const BuildableNode& node) const {
    return prefix(node.external);
}

std::filesystem::path ArtifactContext::object_path(
    const BuildableNode& node, const TranslationUnit& unit) const {
    const auto pfx = prefix(node);
    if (!pfx.empty()) {
        return output_root_ / pfx / (unit.path + ".o");
    }
    return output_root_ / (unit.path + ".o");
}

std::filesystem::path ArtifactContext::bmi_dir() const {
    return output_root_ / "bmi";
}

std::filesystem::path ArtifactContext::executable_path(
    const BuildableNode& node) const {
    return (output_root_ / node.logical_dir / node.name).lexically_normal();
}

std::filesystem::path ArtifactContext::bridge_dir(std::string_view library,
                                                  std::string_view board,
                                                  std::string_view name) const {
    return output_root_ / "external" / library / board / name;
}

std::filesystem::path ArtifactContext::board_object_path(
    const std::filesystem::path& board_source,
    std::string_view /*board_name*/) const {
    if (external_) {
        return output_root_ / "graft/project" / (board_source.string() + ".o");
    }
    return output_root_ / (board_source.string() + ".o");
}

bool ArtifactContext::check_artifact_path(
    const std::filesystem::path& path) const {
    if (!valid_) return false;
    return path_contained_in(output_root_, path) &&
           (tree_root_.empty() || path_contained_in(tree_root_, path));
}

bool ArtifactContext::check_install_path(
    const std::filesystem::path& destination,
    const std::filesystem::path& path) const {
    if (!valid_ || external_) return false;
    return path_contained_in(destination, path) &&
           (tree_root_.empty() || path_contained_in(tree_root_, path));
}

void ArtifactContext::record_objects(
    const BuildableNode& target, std::vector<std::filesystem::path> objects) {
    target_objects_[{target.external, target.logical_dir}] = std::move(objects);
}

const std::vector<std::filesystem::path>& ArtifactContext::objects(
    const BuildableNode& target) const {
    static const std::vector<std::filesystem::path> empty;
    const auto it = target_objects_.find({target.external, target.logical_dir});
    if (it != target_objects_.end()) return it->second;
    return empty;
}

void ArtifactContext::record_board_objects(
    std::string_view board_name, std::vector<std::filesystem::path> objects) {
    board_objects_[std::string(board_name)] = std::move(objects);
}

const std::vector<std::filesystem::path>& ArtifactContext::board_objects(
    std::string_view board_name) const {
    static const std::vector<std::filesystem::path> empty;
    const auto it = board_objects_.find(std::string(board_name));
    if (it != board_objects_.end()) return it->second;
    if (board_name.empty() && !board_objects_.empty()) {
        return board_objects_.begin()->second;
    }
    return empty;
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
            ArtifactContext& context,
            const std::vector<std::filesystem::path>& include_directories) {
    std::error_code ec;

    const bool clang_modules = uses_clang_modules(toolchain);
    const auto bmi_dir = context.bmi_dir();
    if (clang_modules) {
        if (!context.check_artifact_path(bmi_dir)) {
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

    if (target.kind == "app" && !target.sketches.empty()) {
        const std::filesystem::path app_dir = target.source_dir.empty()
            ? std::filesystem::path(target.dir) : target.source_dir;
        const std::filesystem::path main_path = app_dir / "main.cpp";
        const std::filesystem::path manifest_path = app_dir / "mm.mdy";

        for (const auto& s : target.sketches) {
            const std::filesystem::path s_path = app_dir / s;
            if (!safe_exists(s_path)) {
                std::cerr << "build: sketch source does not exist: " << s_path.string() << "\n";
                return exit_manifest;
            }
        }

        bool needs_generation = false;
        if (!safe_exists(main_path)) {
            needs_generation = true;
        } else {
            const auto main_time = std::filesystem::last_write_time(main_path, ec);
            if (!ec) {
                if (safe_exists(manifest_path)) {
                    const auto manifest_time = std::filesystem::last_write_time(manifest_path, ec);
                    if (!ec && main_time < manifest_time) {
                        needs_generation = true;
                    }
                }
                if (!needs_generation) {
                    for (const auto& s : target.sketches) {
                        const std::filesystem::path s_path = app_dir / s;
                        const auto s_time = std::filesystem::last_write_time(s_path, ec);
                        if (!ec && main_time < s_time) {
                            needs_generation = true;
                            break;
                        }
                    }
                }
            } else {
                needs_generation = true;
            }
        }

        if (needs_generation) {
            std::filesystem::path sketch_exe =
                context.tools_dir().empty()
                    ? (std::filesystem::current_path()
                       / "out/bin/sketch")
                    : (context.tools_dir() / "sketch");
            std::error_code sec;
            sketch_exe = std::filesystem::weakly_canonical(
                sketch_exe, sec);
            if (sec || !safe_exists(sketch_exe)) {
                std::cerr
                    << "build: sketch tool does not exist: "
                    << sketch_exe.string() << "\n";
                return exit_compile;
            }
            const std::string command =
                shell_quote(sketch_exe) + " "
                + shell_quote(app_dir);
            const int status = run(toolchain, command);
            if (status != 0) {
                std::cerr
                    << "build: sketch generation failed"
                       " for "
                    << target.name << "\n";
                return exit_compile;
            }
        }

        // An application declaring sketch-library needs
        // byte-correct generated files (main.cpp and the
        // compatibility header). The check-generate-check
        // sequence keeps mm.build from importing mm.ino:
        //   1. silent --check (>/dev/null 2>&1)
        //   2. on failure: sketch <app> (repair)
        //   3. loud --check (stderr visible)
        // Only the second check's diagnostic is printed,
        // which is always the current truth.
        if (!target.sketch_libraries.empty()) {
            std::filesystem::path sketch_exe =
                context.tools_dir().empty()
                    ? (std::filesystem::current_path()
                       / "out/bin/sketch")
                    : (context.tools_dir() / "sketch");
            std::error_code sec;
            sketch_exe = std::filesystem::weakly_canonical(
                sketch_exe, sec);
            if (sec || !safe_exists(sketch_exe)) {
                std::cerr
                    << "build: sketch tool does not exist: "
                    << sketch_exe.string() << "\n";
                return exit_compile;
            }
            const auto quoted_exe =
                shell_quote(sketch_exe);
            const auto quoted_dir =
                shell_quote(app_dir);
            const std::string check_cmd =
                quoted_exe + " --check " + quoted_dir;
            const std::string silent_check =
                check_cmd + " >/dev/null 2>&1";
            const int first =
                run(toolchain, silent_check);
            if (first != 0) {
                const std::string gen_cmd =
                    quoted_exe + " " + quoted_dir;
                const int gen =
                    run(toolchain, gen_cmd);
                if (gen != 0) {
                    std::cerr
                        << "build: sketch generation"
                           " failed for "
                        << target.name << "\n";
                    return exit_compile;
                }
                const int second =
                    run(toolchain, check_cmd);
                if (second != 0) {
                    std::cerr
                        << "build: sketch --check"
                           " failed for "
                        << target.name << "\n";
                    return exit_compile;
                }
            }
        }
    }

    std::vector<std::filesystem::path> compiled_objects;
    for (const auto& source : target.sources) {
        const auto src_path =
            source.source.empty() ? std::filesystem::path(source.path)
                                  : source.source;
        if (!safe_exists(src_path)) {
            std::cerr << "build: source does not exist: " << source.path << "\n";
            return exit_manifest;
        }

        const auto object = (target.kind == "board")
            ? context.board_object_path(source.path, target.name)
            : context.object_path(target, source);

        if (!context.check_artifact_path(object)) {
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
        if (!clang_modules) {
            command += " -fmodules-ts -fmodule-mapper=" +
                       shell_quote(bmi_dir / "gcc.mapper") + " -x c++";
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
                if (!context.check_artifact_path(bmi)) {
                    std::cerr << "build: refusing to write outside the project: " << bmi.string()
                              << "\n";
                    return exit_manifest;
                }
                command += " -fmodule-output=" + shell_quote(bmi);
            }
        }
        command +=
            " -c " + shell_quote(src_path) + " -o " + shell_quote(object);
        if (run(toolchain, command) != 0) {
            std::cerr << "build: failed to compile " << source.path << "\n";
            return exit_compile;
        }

        compiled_objects.push_back(object);
        target.objects.push_back(object);
    }

    if (target.kind == "board") {
        context.record_board_objects(target.name, compiled_objects);
    } else {
        context.record_objects(target, compiled_objects);
    }

    return exit_ok;
}

int compile(const Toolchain& toolchain, BuildableNode& target,
            const std::filesystem::path& build_dir,
            const std::vector<std::filesystem::path>& include_directories) {
    ArtifactContext context(".", build_dir);
    return compile(toolchain, target, context, include_directories);
}

int link(const Toolchain& toolchain,
         const std::vector<std::filesystem::path>& objects,
         const std::filesystem::path& output,
         const std::vector<std::string>& link_inputs,
         const ArtifactContext* context) {
    const bool allowed = context != nullptr
                             ? context->check_artifact_path(output)
                             : within_root(output);
    if (!allowed) {
        std::cerr << "build: refusing to link outside the project: "
                  << output.string() << "\n";
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
    // After the objects, because a linker resolves left to right and a library
    // only answers references it has already seen. The grammar for a link-input
    // is checked at manifest load, so no value here needs quoting to be safe.
    for (const auto& input : link_inputs) command += " -l" + input;
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
            const std::string& name, const std::filesystem::path& tree_root) {
    if (!is_safe_name(name)) {
        std::cerr << "build: refusing to install to unsafe name: " << name << "\n";
        return exit_link;
    }

    const auto installed = bin_dir / name;
    std::error_code ec_root;
    const auto eff_root =
        tree_root.empty() ? std::filesystem::current_path(ec_root) : tree_root;
    if (!within_destination(bin_dir, installed, eff_root)) {
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

bool prepare_module_cache(const Toolchain& toolchain, const Tree& tree,
                          const ArtifactContext& context) {
    std::error_code ec;
    const auto bmi_dir = context.bmi_dir();
    if (!context.check_artifact_path(bmi_dir)) {
        std::cerr << "build: refusing to clear outside the project: " << bmi_dir.string() << "\n";
        return false;
    }
    std::filesystem::remove_all(bmi_dir, ec);
    if (ec) {
        std::cerr << "build: cannot clear " << bmi_dir.string() << ": " << ec.message() << "\n";
        return false;
    }
    std::filesystem::create_directories(bmi_dir, ec);
    if (ec) {
        std::cerr << "build: cannot create " << bmi_dir.string() << ": " << ec.message()
                  << "\n";
        return false;
    }

    // Keep GCC mapper generation keyed to the declared family. Apple
    // compatibility is handled by compile() so this deterministic cache
    // contract remains testable and stable for explicit GCC toolchains.
    if (toolchain.family != CompilerFamily::Gcc) return true;

    std::map<std::string, std::string, std::less<>> mappings;
    for (const auto& target : tree.targets) {
        for (const auto& source : target.sources) {
            std::string module_name = source.module_name;
            if (module_name.empty() && target.kind == "module" &&
                std::filesystem::path(source.path).extension() == ".cppm")
                module_name = target.module_name;
            if (module_name.empty()) continue;

            std::string filename = module_name;
            for (char& c : filename)
                if (c == ':') c = '-';
            mappings.emplace(std::move(module_name), std::move(filename) + ".gcm");
        }
    }

    const auto root = bmi_dir.generic_string();
    if (root.find_first_of(" \t\r\n") != std::string::npos) {
        std::cerr << "build: GCC module cache path contains whitespace: " << root << "\n";
        return false;
    }
    const auto mapper_path = bmi_dir / "gcc.mapper";
    std::ofstream mapper(mapper_path);
    if (!mapper) {
        std::cerr << "build: cannot write GCC module mapper: " << mapper_path.string()
                  << "\n";
        return false;
    }
    mapper << "$root " << root << '\n';
    for (const auto& [module_name, filename] : mappings)
        mapper << module_name << ' ' << filename << '\n';
    mapper.close();
    if (!mapper) {
        std::cerr << "build: cannot finish GCC module mapper: " << mapper_path.string()
                  << "\n";
        return false;
    }
    return true;
}

bool prepare_module_cache(const Toolchain& toolchain, const Tree& tree,
                          const std::filesystem::path& build_dir) {
    ArtifactContext context(".", build_dir);
    return prepare_module_cache(toolchain, tree, context);
}
}
