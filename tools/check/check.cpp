// modules.cpp check tool
//
// Usage: check [-v|--verbose] [-h|--help] [<path to mm.mdy>]
//        (default: mm.mdy in the current dir)
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
bool beneath(const mm::build::Project& project, std::size_t node, std::size_t scope) {
    for (auto current = node; current != mm::build::no_parent;
         current = project.nodes[current].parent)
        if (current == scope) return true;
    return false;
}

void collect(const mm::build::Project& project, std::size_t scope,
             std::set<std::string>& seen, std::vector<std::filesystem::path>& files) {
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (!beneath(project, i, scope) || project.target[i] == mm::build::no_target) continue;
        const mm::build::BuildableNode* target = nullptr;
        if (project.nodes[i].kind == "test") target = &project.tests[project.target[i]];
        else if (project.nodes[i].kind == "module" || project.nodes[i].kind == "app")
            target = &project.targets[project.target[i]];
        if (target == nullptr) continue;
        for (const auto& unit : target->sources)
            if (seen.insert(unit.path).second) files.emplace_back(unit.path);
    }
    for (const auto& board : project.boards) {
        if (!beneath(project, board.node, scope)) continue;
        for (const auto& source : board.sources)
            if (seen.insert(source.generic_string()).second) files.push_back(source);
    }
}

}

int main(int argc, char** argv) {
    mm::app::Options options("check");
    options.help("check [-v|--verbose] [-h|--help] [manifest]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;

    const bool verbose = options.verbose();
    auto manifest_path = options.positional().empty()
                             ? std::filesystem::path("mm.mdy")
                             : std::filesystem::path(options.positional().front());
    manifest_path = mm::build::resolve_manifest(manifest_path);

    std::filesystem::path root;
    if (const auto status = mm::app::open_manifest("check", manifest_path, root, false);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage : mm::build::exit_manifest;

    std::cout << "modules.cpp check tool\n";
    std::cout << "  root " << root.string() << "\n\n";

    auto project_root = mm::build::find_project_root(root);
    if (project_root.empty()) {
        std::cerr << "check: no kind:project mm.mdy above " << root.string() << "\n";
        return mm::build::exit_manifest;
    }
    std::error_code ec;
    const auto requested_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return mm::build::exit_manifest;
    std::filesystem::current_path(project_root, ec);
    if (ec) {
        std::cerr << "check: cannot enter project root: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }
    auto project = mm::build::load_project(".", {.tool = "check"});
    if (!project.ok) return mm::build::exit_manifest;
    std::size_t scope = mm::build::no_parent;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto directory = std::filesystem::weakly_canonical(project.nodes[i].dir, ec);
        if (!ec && directory == requested_root) {
            scope = i;
            break;
        }
        ec.clear();
    }
    if (scope == mm::build::no_parent) {
        std::cerr << "check: requested manifest is not in the project tree\n";
        return mm::build::exit_manifest;
    }

    std::set<std::string> seen;
    std::vector<std::filesystem::path> files;
    collect(project, scope, seen, files);

    // Only for a full project check, not a partial one: tools/build/main.cpp
    // has no manifest anywhere (see the note above main()), so the walk
    // above never reaches it regardless of root, but reaching outside a
    // deliberately narrowed `check modules/mm.mdy` would be surprising.
    if (root == project_root) {
        const auto stage_zero = project_root / "tools/build/main.cpp";
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

    // Sources are stored project-relative even when the requested scope is a
    // subtree. Anchor them to the project root, not the requested directory,
    // so both unit: and file: retain the same meaning in a narrowed check.
    for (const auto& file : files)
        command += " " + mm::build::shell_quote(project_root / file);

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
