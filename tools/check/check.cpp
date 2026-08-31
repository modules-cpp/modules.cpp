// modules.cpp check tool
//
// Usage: check [-v] [<path to mm.mdy>]     (default: mm.mdy in the current dir)
//
// Wraps the installed cppcheck tool to run a selected subset of
// docs/modules-c++20.mdy's rules (see that document's "Enforcement" section
// for exactly which) over every module, application, and test source file
// in the manifest tree. This is not full enforcement of the specification:
// checks not implemented in the addon, and any source file outside the
// manifest tree that the addon does not get a fixed exception for, are not
// covered by a clean result here. The rules themselves live in
// tools/check/cppcheck/cpp20_rules.py, a cppcheck addon; this file only
// collects the file list and drives the process. All the file collection
// lives in mm.build; this file is the front end.
//
// The root manifest's folder: tests entry puts kind:test manifests in the
// same single walk as everything else, so tree.tests already covers them;
// this tool does not load a second tree. tools/build/main.cpp is added
// separately, below: it is deliberately unmanifested (build0's source,
// compiled before any manifest exists to declare it; see docs/modules.mdy),
// so the manifest walk alone would silently exclude it despite it being
// among the most security-sensitive files in the project.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

import mm.app;
import mm.build;

namespace {

// tests/main.cpp is the shared runner unit: entry in more than one kind:test
// manifest, so collecting straight into a vector would hand cppcheck the
// same path twice.
void collect(const mm::build::Tree& tree, std::set<std::string>& seen,
             std::vector<std::filesystem::path>& files) {
    for (const auto& target : tree.targets)
        for (const auto& unit : target.sources)
            if (seen.insert(unit.path).second) files.push_back(unit.path);
    for (const auto& target : tree.tests)
        for (const auto& unit : target.sources)
            if (seen.insert(unit.path).second) files.push_back(unit.path);
}

}

int main(int argc, char** argv) {
    std::filesystem::path manifest_path;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (mm::app::verbose_flag(arg))
            verbose = true;
        else if (manifest_path.empty())
            manifest_path = arg;
        else {
            mm::app::unexpected_argument("check", arg);
            return mm::build::exit_usage;
        }
    }

    if (manifest_path.empty()) manifest_path = "mm.mdy";
    manifest_path = mm::build::resolve_manifest(manifest_path);

    std::filesystem::path root;
    if (const auto status = mm::app::open_manifest("check", manifest_path, root, true);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage : mm::build::exit_manifest;

    std::cout << "modules.cpp check tool\n";
    std::cout << "  root " << root.string() << "\n\n";

    auto tree = mm::build::load_tree(".");
    if (!tree.ok) return mm::build::exit_manifest;

    std::set<std::string> seen;
    std::vector<std::filesystem::path> files;
    collect(tree, seen, files);

    // The addon lives at a fixed path under the project root, not under
    // root: root is wherever the given manifest lives, which is the project
    // root for a plain `check` but a subdirectory for a partial check such
    // as `check modules/mm.mdy`.
    auto project_root = mm::build::find_project_root(root);
    if (project_root.empty()) {
        std::cerr << "check: no kind:project mm.mdy above " << root.string() << "\n";
        return mm::build::exit_manifest;
    }

    // Only for a full project check, not a partial one: tools/build/main.cpp
    // has no manifest anywhere (see the note above main()), so the walk
    // above never reaches it regardless of root, but reaching outside a
    // deliberately narrowed `check modules/mm.mdy` would be surprising.
    if (root == project_root) {
        const auto stage_zero = project_root / "tools/build/main.cpp";
        std::error_code ec;
        const auto relative_stage_zero = std::filesystem::relative(stage_zero, root, ec);
        if (!ec && std::filesystem::exists(stage_zero) &&
            seen.insert(relative_stage_zero.string()).second)
            files.push_back(relative_stage_zero);
    }

    if (files.empty()) {
        std::cerr << "check: manifest tree declares no source files\n";
        return mm::build::exit_manifest;
    }

    const auto addon = project_root / "tools/check/cppcheck/cpp20_rules.py";
    if (!std::filesystem::exists(addon)) {
        std::cerr << "check: addon not found: " << addon.string() << "\n";
        return mm::build::exit_manifest;
    }

    // syntaxError and missingIncludeSystem come from cppcheck not
    // understanding C++20 modules and not being given system include paths;
    // neither is a rule this project enforces, and leaving them enabled
    // would make --error-exitcode fire on file after file for reasons that
    // have nothing to do with docs/modules-c++20.mdy.
    constexpr int exit_violations = 1;
    std::string command =
        "cppcheck --std=c++20 --language=c++ --quiet"
        " --suppress=syntaxError --suppress=missingIncludeSystem"
        " --error-exitcode=" + std::to_string(exit_violations) +
        " --addon=" + mm::build::shell_quote(addon);
    if (verbose) command += " --verbose";

    for (const auto& file : files)
        command += " " + mm::build::shell_quote(file);

    std::cout << "Checking " << files.size() << " source file(s) against "
              << addon.string() << "\n\n";

    const auto toolchain = mm::build::default_toolchain(verbose);
    const int status = mm::build::run(toolchain, command);
    if (status < 0) {
        std::cerr << "check: failed to run cppcheck\n";
        return mm::build::exit_run;
    }

    if (status == mm::build::exit_ok)
        std::cout << "\ncheck: no violations found in " << files.size() << " file(s)\n";

    return status;
}
