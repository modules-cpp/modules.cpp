// modules.cpp build tool, stage 1
//
// Usage: build [-v] [--host | --target] [<path to mm.mdy>]
//
// Built by stage 0 (tools/build/main.cpp), which exists only to produce this
// binary. All the work lives in mm.build; this file is the front end.
//
// Walks the manifest tree from the given root, orders every kind:module and
// kind:app target by its use: edges, compiles and links them, and installs the
// host app binaries. Target app binaries remain in their configured lane.
// kind:test targets are counted but not built: running tests is
// tools/test's job, and a build that stops on a failing test cannot be used to
// fix it.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>
#include <vector>

import mm.build;
import mm.configure;

int main(int argc, char** argv) {
    std::filesystem::path manifest_path;
    bool verbose = false;
    bool requested_host = false;
    bool requested_target = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else if (arg == "--host") {
            if (requested_host) {
                std::cerr << "build: --host may be given only once\n";
                return mm::build::exit_usage;
            }
            requested_host = true;
        } else if (arg == "--target") {
            if (requested_target) {
                std::cerr << "build: --target may be given only once\n";
                return mm::build::exit_usage;
            }
            requested_target = true;
        }
        else if (arg.starts_with('-')) {
            std::cerr << "build: unknown option: " << arg << "\n";
            return mm::build::exit_usage;
        } else if (manifest_path.empty())
            manifest_path = arg;
        else {
            std::cerr << "build: unexpected argument: " << arg << "\n";
            return mm::build::exit_usage;
        }
    }
    if (requested_host && requested_target) {
        std::cerr << "build: --host and --target are mutually exclusive\n";
        return mm::build::exit_usage;
    }

    if (manifest_path.empty()) manifest_path = "mm.mdy";
    manifest_path = mm::build::resolve_manifest(manifest_path);

    if (manifest_path.filename() != "mm.mdy") {
        std::cerr << "build: not an mm.mdy manifest: " << manifest_path.string() << "\n";
        return mm::build::exit_usage;
    }
    if (!std::filesystem::exists(manifest_path)) {
        std::cerr << "build: manifest does not exist: " << manifest_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto root = std::filesystem::absolute(manifest_path).parent_path();
    const auto found_project_root = mm::build::find_project_root(root);
    const auto project_root = found_project_root.empty() ? root : found_project_root;
    auto tree_root = root.lexically_relative(project_root);
    if (tree_root.empty()) tree_root = ".";

    std::error_code ec;
    const auto requested_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        std::cerr << "build: cannot resolve " << root.string() << ": " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }
    std::filesystem::current_path(project_root, ec);
    if (ec) {
        std::cerr << "build: cannot enter " << project_root.string() << ": " << ec.message()
                  << "\n";
        return mm::build::exit_manifest;
    }

    std::cout << "modules.cpp build tool\n";
    std::cout << "  root " << project_root.string() << "\n";

    mm::build::BuildConfiguration configuration;
    if (!mm::build::resolve_configuration(".", verbose, configuration))
        return mm::build::exit_manifest;

    const bool target_lane = requested_target || (!requested_host && configuration.selects_cross());
    const auto* toolchain_ptr = configuration.toolchain_for(target_lane);
    const auto* build_dir_ptr = configuration.build_directory_for(target_lane);
    if (toolchain_ptr == nullptr || build_dir_ptr == nullptr) {
        std::cerr << "build: target lane is not configured\n";
        return mm::build::exit_manifest;
    }
    const auto& toolchain = *toolchain_ptr;
    const auto& build_dir = *build_dir_ptr;
    if (!mm::configure::log_configuration({
            .tool = "build",
            .build = mm::build::build_name(configuration.build),
            .compiler_family = mm::build::compiler_family_name(toolchain.family),
            .compiler = toolchain.compiler.invocation,
            .compile_flags = toolchain.compiler.arguments,
            .link_flags = toolchain.linker.arguments,
            .target = build_dir,
            .verbose = verbose,
        }))
        return mm::build::exit_manifest;
    std::cout << "\n";

    auto project = mm::build::load_project(".", {.tool = "build", .warn_options = true});
    if (!project.ok) return mm::build::exit_manifest;

    std::size_t scope = mm::build::no_parent;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto directory = std::filesystem::weakly_canonical(project.nodes[i].dir, ec);
        if (ec) return mm::build::exit_manifest;
        if (directory == requested_root) {
            scope = i;
            break;
        }
    }
    if (scope == mm::build::no_parent) {
        std::cerr << "build: requested manifest is not in the project tree: "
                  << manifest_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    std::vector<bool> in_scope(project.nodes.size(), false);
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        for (auto current = i; current != mm::build::no_parent;
             current = project.nodes[current].parent) {
            if (current != scope) continue;
            in_scope[i] = true;
            break;
        }
    }

    mm::build::BuildCapabilities capabilities;
    if (!mm::build::resolve_capabilities(".", configuration.build, project, capabilities,
                                         "build"))
        return mm::build::exit_manifest;

    const auto& buildable = target_lane ? capabilities.target : capabilities.host;
    std::vector<std::size_t> old_to_new(project.targets.size(), mm::build::no_target);
    mm::build::Tree tree;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if ((project.nodes[i].kind == "module" || project.nodes[i].kind == "app") &&
            project.target[i] != mm::build::no_target && buildable[i]) {
            const auto old = project.target[i];
            if (old_to_new[old] == mm::build::no_target) {
                old_to_new[old] = tree.targets.size();
                tree.targets.push_back(std::move(project.targets[old]));
            }
        }
    }

    std::vector<std::size_t> roots;
    std::size_t declared_targets = 0;
    std::size_t unavailable_targets = 0;
    std::size_t skipped_tests = 0;
    std::size_t skipped_docs = 0;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (!in_scope[i]) continue;
        if (project.nodes[i].kind == "module" || project.nodes[i].kind == "app") {
            ++declared_targets;
            if (!buildable[i])
                ++unavailable_targets;
            else
                roots.push_back(old_to_new[project.target[i]]);
        } else if (project.nodes[i].kind == "test") {
            ++skipped_tests;
        } else if (project.nodes[i].kind == "doc") {
            ++skipped_docs;
        }
    }

    if (declared_targets == 0) {
        std::cerr << "build: manifest tree declares no module or app targets\n";
        return mm::build::exit_manifest;
    }

    if (roots.empty()) {
        std::cout << declared_targets << " module/app target(s) skipped; not buildable-"
                  << (target_lane ? "target" : "host") << "\n";
        return mm::build::exit_ok;
    }

    std::vector<std::size_t> order;
    std::vector<bool> ordered(tree.targets.size(), false);
    for (const auto root_index : roots) {
        std::vector<std::size_t> root_order;
        if (!mm::build::order_from(tree, root_index, root_order))
            return mm::build::exit_manifest;
        for (const auto index : root_order) {
            if (ordered[index]) continue;
            ordered[index] = true;
            order.push_back(index);
        }
    }

    std::cout << "Clear module cache\n";
    if (!mm::build::clear_module_cache(build_dir)) {
        std::cerr << "build: failed to clear module cache\n";
        return mm::build::exit_compile;
    }
    std::cout << "\n";

    std::cout << "Compile\n";
    for (const auto index : order) {
        auto& target = tree.targets[index];
        std::cout << "  " << target.kind << " " << target.name << "\n";

        if (const int status = mm::build::compile(toolchain, target, build_dir); status != 0)
            return status;
    }

    // Linked artifacts follow the configured build directory, but installed
    // host tools keep their stable launcher path. In particular, ./build and
    // build.sh execute out/bin/build, so a configured build must replace that
    // executable rather than strand the new version under the configured tree.
    const std::filesystem::path bin_dir = "out/bin";

    std::cout << "\nLink\n";
    for (const auto index : order) {
        const auto& target = tree.targets[index];
        if (target.kind != "app") continue;

        const auto output = build_dir / (target.dir / target.name).lexically_normal();

        std::cout << "  app " << target.name << " -> " << output.string() << "\n";

        const auto objects = mm::build::closure(tree, index);
        if (const int status = mm::build::link(toolchain, objects, output); status != 0)
            return status;

        if (!target_lane) {
            if (const int status = mm::build::install(output, bin_dir, target.name); status != 0)
                return status;
        }
    }

    if (!target_lane) std::cout << "\nInstalled to " << bin_dir.string() << "\n";
    if (unavailable_targets != 0)
        std::cout << unavailable_targets << " module/app target(s) skipped; not buildable-"
                  << (target_lane ? "target" : "host") << "\n";
    if (skipped_tests != 0)
        std::cout << skipped_tests << " kind:test target(s) skipped; run them with tools/test\n";
    if (skipped_docs != 0)
        std::cout << skipped_docs << " kind:doc target(s) skipped; documentation is not built\n";

    return mm::build::exit_ok;
}
