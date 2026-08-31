// modules.cpp model tool
//
// Usage: model [-v] [--configuration] [--tools] [<path to mm.mdy>]
//        (default: mm.mdy in the current dir)
//
// Checks the real project against itself, through the models.* abstract
// data model (models/) rather than mm::build's own structures directly:
// mm.model adapts mm::build's and mm::mdy's data onto models::Repository,
// and this front end walks only that abstraction.
//
// Two checks, both structural rather than textual, so neither depends on
// parsing docs/modules.mdy's prose:
//
//   - dependency resolution: every use: entry on a module, app, or test
//     names a module: that some kind:module manifest actually exports.
//     mm::build::order already rejects an unknown module at build time;
//     this re-derives the same fact independently, through the model.
//   - build completeness: every models::Tool's invocation() exists on disk,
//     i.e. the tree that's declared matches what has actually been built.
//     This covers every kind:app manifest's installed out/bin/<name>, plus
//     build0 and build1 (out/build0, out/build1), which mm.model also
//     represents as Tools despite having no kind:app manifest of their own.
//
// --configuration additionally reports the project's build configuration
// (models.configuration, via mm::model::default_configuration): the live
// compiler/flags/verbose plus the fixed platform/locale/shell policy.
// Independent of the manifest tree, so it is reported even when the checks
// above fail to load one.
//
// --tools additionally lists every models::Tool the loaded tree produces:
// name, provenance, invocation(), and the app that declares it, if any.
// Unlike --configuration this needs the tree loaded, so it is reported
// after that succeeds, alongside the two checks rather than independent of
// them.
//
// All the work lives in mm.model; this file is the front end.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
import mm.model;
import models.configuration;
import models.manifest;
import models.tool;

namespace {

// Every use: on a module, app, or test, paired with the node that declared
// it, so a mismatch can be reported against something a reader can find.
struct Dependency {
    std::string_view declarer;
    std::string_view used;
};

void collect_uses(const std::vector<const models::BuildableNode*>& nodes,
                   std::vector<Dependency>& out) {
    for (const auto* node : nodes)
        for (const auto used : node->uses()) out.push_back({node->name(), used});
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path manifest_path;
    bool verbose = false;
    bool report_configuration = false;
    bool report_tools = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else if (arg == "--configuration")
            report_configuration = true;
        else if (arg == "--tools")
            report_tools = true;
        else if (manifest_path.empty())
            manifest_path = arg;
        else {
            std::cerr << "model: unexpected argument: " << arg << "\n";
            return mm::build::exit_usage;
        }
    }

    if (manifest_path.empty()) manifest_path = "mm.mdy";
    manifest_path = mm::build::resolve_manifest(manifest_path);

    if (manifest_path.filename() != "mm.mdy") {
        std::cerr << "model: not an mm.mdy manifest: " << manifest_path.string() << "\n";
        return mm::build::exit_usage;
    }
    if (!std::filesystem::exists(manifest_path)) {
        std::cerr << "model: manifest does not exist: " << manifest_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto root = std::filesystem::absolute(manifest_path).parent_path();

    std::cout << "modules.cpp model tool\n";
    std::cout << "  root " << root.string() << "\n\n";

    if (report_configuration) {
        // Independent of the manifest tree: default_configuration() needs
        // no root_dir, so this is reported even if the tree below fails to
        // load.
        const auto configuration = mm::model::default_configuration(verbose);
        std::cout << "Configuration\n";
        std::cout << "  compiler       " << configuration->compiler() << "\n";
        std::cout << "  compiler flags " << configuration->compiler_flags() << "\n";
        std::cout << "  linker flags   " << configuration->linker_flags() << "\n";
        std::cout << "  verbose        " << (configuration->verbose() ? "true" : "false") << "\n";
        std::cout << "  platform       " << configuration->platform() << "\n";
        std::cout << "  locale         " << configuration->locale() << "\n";
        std::cout << "  shell          " << configuration->shell() << "\n\n";
    }

    bool ok = false;
    auto loaded = mm::model::Loaded::load(root, ok);
    if (!ok) {
        std::cerr << "model: failed to load the manifest tree\n";
        return mm::build::exit_manifest;
    }

    const auto& repository = loaded.repository();
    const auto modules = repository.modules();
    const auto apps = repository.apps();
    const auto tests = repository.tests();

    if (report_tools) {
        const auto tools_list = loaded.tools();
        std::cout << "Tools\n";
        for (const auto* tool : tools_list) {
            std::cout << "  " << tool->name() << "\n";
            std::cout << "    provenance  "
                      << (tool->provenance() == models::Provenance::BuiltIn ? "BuiltIn" : "ThirdParty")
                      << "\n";
            std::cout << "    invocation  " << tool->invocation().string() << "\n";
            std::cout << "    declared by "
                      << (tool->declared_by() != nullptr ? tool->declared_by()->name() : "(none)")
                      << "\n";
        }
        std::cout << "\n";
    }

    if (verbose) {
        std::cout << "  modules " << modules.size() << "\n";
        std::cout << "  apps    " << apps.size() << "\n";
        std::cout << "  tests   " << tests.size() << "\n\n";
    }

    std::set<std::string_view> known_modules;
    for (const auto* node : modules) known_modules.insert(node->exported_module_name());

    std::vector<const models::BuildableNode*> buildable;
    for (const auto* node : modules) buildable.push_back(node);
    for (const auto* node : apps) buildable.push_back(node);
    for (const auto* node : tests) buildable.push_back(node);

    std::vector<Dependency> dependencies;
    collect_uses(buildable, dependencies);

    int violations = 0;

    std::cout << "Dependency resolution\n";
    for (const auto& dependency : dependencies) {
        if (known_modules.contains(dependency.used)) continue;
        std::cout << "  FAIL " << dependency.declarer << " uses: " << dependency.used
                  << " (no module exports this name)\n";
        ++violations;
    }
    if (violations == 0) std::cout << "  ok: " << dependencies.size() << " use: entries resolved\n";

    std::cout << "\nBuild completeness\n";
    const auto tools = loaded.tools();
    int missing = 0;
    int checked = 0;
    for (const auto* tool : tools) {
        // ThirdParty tools such as c++ or cppcheck are resolved from $PATH,
        // not a root relative path: invocation() is a bare command name for
        // them, and checking it against root would always fail.
        if (tool->provenance() != models::Provenance::BuiltIn) continue;
        ++checked;

        const auto binary = root / tool->invocation();
        if (std::filesystem::exists(binary)) continue;
        std::cout << "  FAIL " << tool->name() << " has no installed binary at "
                  << binary.string() << "\n";
        ++missing;
    }
    if (missing == 0)
        std::cout << "  ok: " << checked << " tool(s) exist at their declared invocation()\n";
    violations += missing;

    std::cout << "\n";
    if (violations == 0) {
        std::cout << "model: no violations found\n";
        return mm::build::exit_ok;
    }

    std::cout << "model: " << violations << " violation(s) found\n";
    return 1;
}
