// modules.cpp test tool
//
// Usage: test [-v|--verbose] [-h|--help] [--host | --target] [--compile-only]
//             <path to a kind:test mm.mdy>
//
// Reads a test manifest, compiles every declared unit in order, links the
// objects directly into one test binary, and either stops for --compile-only
// or runs a host binary and propagates its exit code. Target execution is
// rejected until configuration can represent a target runner.
// All the work lives in mm.build; this file is the front end. The rules it
// relies on are specified by docs/modules-test.mdy.
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
import mm.configure;

int main(int argc, char** argv) {
    mm::app::Options options("test");
    options.flag("--host");
    options.flag("--target");
    options.flag("--compile-only");
    options.help("test [-v|--verbose] [-h|--help] [--host | --target] [--compile-only] "
                 "<test-manifest>");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;

    if (options.count("--host") > 1 || options.count("--target") > 1 ||
        options.count("--compile-only") > 1) {
        std::cerr << "test: option may be given only once\n";
        return mm::build::exit_usage;
    }
    if (options.seen("--host") && options.seen("--target")) {
        std::cerr << "test: --host and --target are mutually exclusive\n";
        return mm::build::exit_usage;
    }

    const bool verbose = options.verbose();
    const bool compile_only = options.seen("--compile-only");

    // The one tool with no default manifest: a test target must be named.
    if (options.positional().empty()) {
        std::cerr << "usage: test [-v] [--host | --target] [--compile-only] <path to mm.mdy>\n";
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
    auto target = mm::build::load_test(manifest_path, ok, {.tool = "test", .warn_options = true});
    if (!ok) return mm::build::exit_manifest;

    const auto root = mm::build::find_project_root(manifest_dir);
    if (root.empty()) {
        std::cerr << "test: no kind:project mm.mdy above " << manifest_dir.string() << "\n";
        return mm::build::exit_manifest;
    }

    std::error_code ec;
    const auto requested_manifest = std::filesystem::weakly_canonical(manifest_path, ec);
    if (ec) {
        std::cerr << "test: cannot resolve manifest: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    const auto name = target.name;
    const auto units = target.sources.size();
    const auto uses = target.uses.size();

    // TranslationUnit paths are root relative, and the compiler writes gcm.cache into the
    // working directory, so both want the project root.
    std::filesystem::current_path(root, ec);
    if (ec) {
        std::cerr << "test: cannot enter project root: " << ec.message() << "\n";
        return mm::build::exit_manifest;
    }

    mm::build::BuildConfiguration configuration;
    if (!mm::build::resolve_configuration(".", verbose, configuration))
        return mm::build::exit_manifest;
    const bool target_lane = options.seen("--target") ||
                             (!options.seen("--host") && configuration.selects_cross());
    const auto* toolchain_ptr = configuration.toolchain_for(target_lane);
    const auto* lane_directory = configuration.build_directory_for(target_lane);
    if (toolchain_ptr == nullptr || lane_directory == nullptr) {
        std::cerr << "test: target lane is not configured\n";
        return mm::build::exit_manifest;
    }
    const auto& toolchain = *toolchain_ptr;
    const auto build_dir = *lane_directory / "tests" / name;

    auto project = mm::build::load_project(".", {.tool = "test", .warn_options = true});
    if (!project.ok) return mm::build::exit_manifest;

    mm::build::BuildCapabilities capabilities;
    if (!mm::build::resolve_capabilities(".", configuration.build, project, capabilities, "test"))
        return mm::build::exit_manifest;

    std::size_t test_node = mm::build::no_target;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        const auto candidate = std::filesystem::weakly_canonical(project.nodes[i].manifest, ec);
        if (ec) {
            std::cerr << "test: cannot resolve manifest: " << ec.message() << "\n";
            return mm::build::exit_manifest;
        }
        if (candidate == requested_manifest) {
            test_node = i;
            break;
        }
    }
    if (test_node == mm::build::no_target || project.nodes[test_node].kind != "test") {
        std::cerr << "test: requested manifest is not a registered test: "
                  << manifest_path.string() << "\n";
        return mm::build::exit_manifest;
    }

    const auto buildable = capabilities.lane(
        target_lane, configuration.target_has_host_capability());
    if (!buildable[test_node]) {
        std::cerr << "test: " << project.nodes[test_node].manifest.string() << ": " << name
                  << " is not buildable-" << (target_lane ? "target" : "host") << "\n";
        return mm::build::exit_manifest;
    }
    if (target_lane && !compile_only) {
        std::cerr << "test: target " << toolchain.target
                  << " has no test runner; use --compile-only\n";
        return mm::build::exit_manifest;
    }

    mm::build::Tree tree;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if ((project.nodes[i].kind == "module" || project.nodes[i].kind == "app") &&
            project.target[i] != mm::build::no_target && buildable[i])
            tree.targets.push_back(std::move(project.targets[project.target[i]]));
    }

    std::cout << "modules.cpp test tool\n";
    std::cout << "  manifest " << manifest_path.string() << "\n";
    std::cout << "  root     " << root.string() << "\n";
    if (!mm::configure::log_configuration({
            .tool = "test",
            .build = mm::build::build_name(configuration.build),
            .compiler_family = mm::build::compiler_family_name(toolchain.family),
            .compiler = toolchain.compiler.invocation,
            .compile_flags = toolchain.compiler.arguments,
            .link_flags = toolchain.linker.arguments,
            .target = root / build_dir,
            .verbose = verbose,
        }))
        return mm::build::exit_manifest;
    std::cout << "  units    " << units << "\n";
    std::cout << "  uses     " << uses << "\n\n";

    // The modules a test uses come from the project tree, not from its own
    // manifest: appending the test as a target lets the ordinary use: machinery
    // resolve them transitively, so a test manifest lists only its own units.
    tree.targets.push_back(std::move(target));
    const auto index = tree.targets.size() - 1;

    std::vector<std::size_t> order;
    if (!mm::build::order_from(tree, index, order)) return mm::build::exit_manifest;

    if (!mm::build::clear_module_cache(build_dir)) return mm::build::exit_compile;

    std::filesystem::remove_all(build_dir, ec);
    if (ec) {
        std::cerr << "test: cannot clear " << build_dir.string() << ": " << ec.message() << "\n";
        return mm::build::exit_compile;
    }

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

    if (compile_only) return mm::build::exit_ok;

    std::cout << "\nRun\n\n";

    const int status = mm::build::run(toolchain, mm::build::shell_quote(binary));
    if (status < 0) {
        std::cerr << "test: failed to run " << binary.string() << "\n";
        return mm::build::exit_run;
    }

    return status;
}
