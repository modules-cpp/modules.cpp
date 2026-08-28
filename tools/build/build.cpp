// modules.cpp build tool, stage 1
//
// Usage: build [-v] [<path to mm.mdy>]     (default: mm.mdy in the current dir)
//
// Built by stage 0 (tools/build/main.cpp), which exists only to produce this
// binary. All the work lives in mm.build; this file is the front end.
//
// Walks the manifest tree from the given root, orders every kind:module and
// kind:app target by its use: edges, compiles and links them, and installs the
// app binaries. kind:test targets are counted but not built: running tests is
// tools/test's job, and a build that stops on a failing test cannot be used to
// fix it.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

import mm.build;

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
            std::cerr << "build: unexpected argument: " << arg << "\n";
            return mm::build::exit_usage;
        }
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

    std::error_code ec;
    std::filesystem::current_path(root, ec);
    if (ec) {
        std::cerr << "build: cannot enter " << root.string() << ": " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    std::cout << "modules.cpp build tool\n";
    std::cout << "  root " << root.string() << "\n\n";

    auto tree = mm::build::load_tree(".");
    if (!tree.ok) return mm::build::exit_manifest;

    if (tree.targets.empty()) {
        std::cerr << "build: manifest tree declares no module or app targets\n";
        return mm::build::exit_manifest;
    }

    std::vector<std::size_t> order;
    if (!mm::build::order(tree, order)) return mm::build::exit_manifest;

    const auto toolchain = mm::build::default_toolchain(verbose);
    const std::filesystem::path build_dir = "out";

    mm::build::clear_module_cache();

    std::cout << "Compile\n";
    for (const auto index : order) {
        auto& target = tree.targets[index];
        std::cout << "  " << target.kind << " " << target.name << "\n";

        if (const int status = mm::build::compile(toolchain, target, build_dir); status != 0)
            return status;
    }

    const auto bin_dir = build_dir / "bin";

    std::cout << "\nLink\n";
    for (const auto index : order) {
        const auto& target = tree.targets[index];
        if (target.kind != "app") continue;

        const auto output = build_dir / (target.dir / target.name).lexically_normal();

        std::cout << "  app " << target.name << " -> " << output.string() << "\n";

        const auto objects = mm::build::closure(tree, index);
        if (const int status = mm::build::link(toolchain, objects, output); status != 0)
            return status;

        if (const int status = mm::build::install(output, bin_dir, target.name); status != 0)
            return status;
    }

    std::cout << "\nInstalled to " << bin_dir.string() << "\n";
    if (!tree.tests.empty())
        std::cout << tree.tests.size() << " kind:test target(s) skipped; run them with tools/test\n";
    if (!tree.docs.empty())
        std::cout << tree.docs.size() << " kind:doc target(s) skipped; documentation is not built\n";

    return mm::build::exit_ok;
}
