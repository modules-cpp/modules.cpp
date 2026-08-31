// modules.cpp shell tool
//
// Usage: shell [-v] [-e NAME=VALUE]... [<path to mm.mdy>] <command>
//        (default manifest: mm.mdy in the current dir)
//
// Wraps the system shell: mm::build::run passes command to /bin/sh via
// std::system. Beyond resolving a project root first, the same way build,
// test, and check do, this tool sets environment variables through
// mm.shell before running command, so the child process launched by run
// inherits them: this is mm.shell's first consumer beyond its own module.
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
import mm.shell;

namespace {

// Splits "NAME=VALUE" for -e. Empty name on a malformed argument.
struct Assignment {
    std::string_view name;
    std::string_view value;
};

Assignment split_assignment(std::string_view text) {
    const auto pos = text.find('=');
    if (pos == std::string_view::npos) return {};
    return {text.substr(0, pos), text.substr(pos + 1)};
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::string_view> positional;
    std::vector<std::string_view> assignments;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (mm::app::verbose_flag(arg)) {
            verbose = true;
        } else if (arg == "-e") {
            if (i + 1 >= argc) {
                std::cerr << "shell: -e requires a NAME=VALUE argument\n";
                return mm::build::exit_usage;
            }
            assignments.push_back(argv[++i]);
        } else {
            positional.push_back(arg);
        }
    }

    std::filesystem::path manifest_path;
    std::string_view command;

    if (positional.size() == 1) {
        manifest_path = "mm.mdy";
        command = positional[0];
    } else if (positional.size() == 2) {
        manifest_path = positional[0];
        command = positional[1];
    } else {
        std::cerr << "usage: shell [-v] [-e NAME=VALUE]... [<path to mm.mdy>] <command>\n";
        return mm::build::exit_usage;
    }

    manifest_path = mm::build::resolve_manifest(manifest_path);

    std::filesystem::path root;
    if (const auto status = mm::app::open_manifest("shell", manifest_path, root, true);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage : mm::build::exit_manifest;

    if (verbose) {
        std::cout << "modules.cpp shell tool\n";
        std::cout << "  root  " << root.string() << "\n";
        std::cout << "  shell " << mm::shell::current_shell().string() << "\n\n";
    }

    for (const auto assignment_text : assignments) {
        const auto assignment = split_assignment(assignment_text);
        if (assignment.name.empty()) {
            std::cerr << "shell: malformed -e argument: " << assignment_text << "\n";
            return mm::build::exit_usage;
        }
        if (!mm::shell::set(assignment.name, assignment.value)) {
            std::cerr << "shell: failed to set " << assignment.name << "\n";
            return mm::build::exit_run;
        }
        if (verbose) std::cout << "  export " << assignment.name << "=" << assignment.value << "\n";
    }

    const auto toolchain = mm::build::default_toolchain(verbose);
    const int status = mm::build::run(toolchain, std::string(command));
    if (status < 0) {
        std::cerr << "shell: failed to run command\n";
        return mm::build::exit_run;
    }

    return status;
}
