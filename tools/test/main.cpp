// modules.cpp test tool
//
// Usage: test [-v] <path to a kind:test mm.mdy>
//
// Reads a test manifest, compiles every declared unit in order, links the
// objects directly into one test binary, runs it and propagates its exit code.
// All the work lives in mm.build; this file is the front end. The rules it
// relies on come from proposals/modules-test.mdy.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

import mm.app;
import mm.build;

int main(int argc, char** argv) {
    mm::app::Options options("test");
    if (options.parse(argc, argv) != mm::app::Cli::ok) return mm::build::exit_usage;

    const bool verbose = options.verbose();

    // The one tool with no default manifest: a test target must be named.
    if (options.positional().empty()) {
        std::cerr << "usage: test [-v] <path to mm.mdy>\n";
        return mm::build::exit_usage;
    }

    auto manifest_path = mm::build::resolve_manifest(options.positional().front());

    // enter_root is false here: the directory this tool needs is not the
    // manifest's own, but the kind:project root found above it below.
    std::filesystem::path manifest_dir;
    if (const auto status = mm::app::open_manifest("test", manifest_path, manifest_dir, false);
        status != mm::app::Cli::ok)
        return status == mm::app::Cli::usage ? mm::build::exit_usage : mm::build::exit_manifest;

    bool ok = false;
    auto target = mm::build::load_test(manifest_path, ok);
    if (!ok) return mm::build::exit_manifest;

    const auto root = mm::build::find_project_root(manifest_dir);
    if (root.empty()) {
        std::cerr << "test: no kind:project mm.mdy above " << manifest_dir.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto name = target.name;
    const auto units = target.sources.size();
    const auto uses = target.uses.size();
    const auto build_dir = std::filesystem::path("out") / "tests" / name;

    std::cout << "modules.cpp test tool\n";
    std::cout << "  manifest " << manifest_path.string() << "\n";
    std::cout << "  root     " << root.string() << "\n";
    std::cout << "  build    " << (root / build_dir).string() << "\n";
    std::cout << "  units    " << units << "\n";
    std::cout << "  uses     " << uses << "\n\n";

    std::error_code ec;

    // TranslationUnit paths are root relative, and the compiler writes gcm.cache into the
    // working directory, so both want the project root.
    std::filesystem::current_path(root, ec);
    if (ec) {
        std::cerr << "test: cannot enter project root: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    // The modules a test uses come from the project tree, not from its own
    // manifest: appending the test as a target lets the ordinary use: machinery
    // resolve them transitively, so a test manifest lists only its own units.
    auto tree = mm::build::load_tree(".");
    if (!tree.ok) return mm::build::exit_manifest;

    tree.targets.push_back(std::move(target));
    const auto index = tree.targets.size() - 1;

    std::vector<std::size_t> order;
    if (!mm::build::order_from(tree, index, order)) return mm::build::exit_manifest;

    if (!mm::build::clear_module_cache()) return mm::build::exit_compile;

    std::filesystem::remove_all(build_dir, ec);
    if (ec) {
        std::cerr << "test: cannot clear " << build_dir.string() << ": " << ec.message() << "\n";
        return mm::build::exit_compile;
    }

    const auto toolchain = mm::build::default_toolchain(verbose);

    std::cout << "Compile\n";
    for (const auto position : order) {
        auto& built = tree.targets[position];
        if (built.kind != "test")
            std::cout << "  " << built.kind << " " << built.name << "\n";

        if (const int status = mm::build::compile(toolchain, built, build_dir); status != 0)
            return status;
    }

    const auto binary = build_dir / name;

    std::cout << "\nLink\n  " << binary.string() << "\n";

    const auto objects = mm::build::closure(tree, index);
    if (const int status = mm::build::link(toolchain, objects, binary); status != 0)
        return status;

    std::cout << "\nRun\n\n";

    const int status = mm::build::run(toolchain, mm::build::shell_quote(binary));
    if (status < 0) {
        std::cerr << "test: failed to run " << binary.string() << "\n";
        return mm::build::exit_run;
    }

    return status;
}
