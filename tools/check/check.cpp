// modules.cpp check tool
//
// Usage: check [-v] [<path to mm.mdy>]     (default: mm.mdy in the current dir)
//
// Wraps the installed cppcheck tool to enforce docs/modules-c++20.mdy's rules
// over every module, application, and test source file in the manifest tree.
// The rules themselves live in tools/check/cppcheck/cpp20_rules.py, a cppcheck
// addon; this file only collects the file list and drives the process. All
// the file collection lives in mm.build; this file is the front end.
//
// The manifest tree reachable from the project root does not include tests/
// (the root manifest has no folder: tests entry; tests are run directly by
// tools/test, one manifest at a time), so this tool loads that subtree
// separately and adds its sources to the same list.
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
        if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else if (manifest_path.empty())
            manifest_path = arg;
        else {
            std::cerr << "check: unexpected argument: " << arg << "\n";
            return mm::build::exit_usage;
        }
    }

    if (manifest_path.empty()) manifest_path = "mm.mdy";
    manifest_path = mm::build::resolve_manifest(manifest_path);

    if (manifest_path.filename() != "mm.mdy") {
        std::cerr << "check: not an mm.mdy manifest: " << manifest_path.string() << "\n";
        return mm::build::exit_usage;
    }
    if (!std::filesystem::exists(manifest_path)) {
        std::cerr << "check: manifest does not exist: " << manifest_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto root = std::filesystem::absolute(manifest_path).parent_path();

    std::error_code ec;
    std::filesystem::current_path(root, ec);
    if (ec) {
        std::cerr << "check: cannot enter " << root.string() << ": " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    std::cout << "modules.cpp check tool\n";
    std::cout << "  root " << root.string() << "\n\n";

    auto tree = mm::build::load_tree(".");
    if (!tree.ok) return mm::build::exit_manifest;

    std::set<std::string> seen;
    std::vector<std::filesystem::path> files;
    collect(tree, seen, files);

    if (std::filesystem::exists("tests")) {
        auto test_tree = mm::build::load_tree("tests");
        if (!test_tree.ok) return mm::build::exit_manifest;
        collect(test_tree, seen, files);
    }

    if (files.empty()) {
        std::cerr << "check: manifest tree declares no source files\n";
        return mm::build::exit_manifest;
    }

    // The addon lives at a fixed path under the project root, not under
    // root: root is wherever the given manifest lives, which is the project
    // root for a plain `check` but a subdirectory for a partial check such
    // as `check modules/mm.mdy`.
    auto project_root = mm::build::find_project_root(root);
    if (project_root.empty()) {
        std::cerr << "check: no kind:project mm.mdy above " << root.string() << "\n";
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
