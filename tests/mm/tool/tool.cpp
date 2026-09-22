// Black box tests for the shared tool front end.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <system_error>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

import mm.app;
import mm.build;
import mm.configure;
import mm.test;
import mm.tool;

namespace {

struct current_directory_guard {
    std::filesystem::path original = std::filesystem::current_path();
    ~current_directory_guard() { std::filesystem::current_path(original); }
};

mm::configure::Settings native_settings() {
    mm::configure::Settings settings;
    settings.host = {
        mm::configure::CompilerFamily::Gcc,
        "c++",
        "host",
        "POSIX",
        "-std=c++20 -fmodules-ts -x c++",
        "-std=c++20",
    };
    settings.host_build_directory = "out";
    return settings;
}

// One minimal host-side project in tree: a kind: project root with one node
// in an "app" folder, and a host-lane configuration record written through
// the real writer. When app is false the node is a kind: module instead of
// a kind: app. When built is true a dummy executable sits where
// ArtifactContext::executable_path puts it.
void write_fixture(const mm::test::scoped_tree& tree, bool app, bool built) {
    tree.manifest("", "kind: project\nname: fixture\nfolder: app\n");
    if (app)
        tree.manifest("app", "kind: app\nname: widget\nfile: a.cpp\n");
    else
        tree.manifest("app", "kind: module\nname: widget\nmodule: mm.widget\n"
                             "file: a.cppm\n");
    if (built) {
        std::error_code ec;
        std::filesystem::create_directories(tree.root() / "out" / "app", ec);
        std::ofstream out(tree.root() / "out" / "app" / "widget", std::ios::binary);
        out << "dummy";
    }
    mm::test::expect(mm::configure::write_configuration(tree.root(), native_settings()),
                     "expected fixture configuration write to succeed");
}

int resolve(const mm::test::scoped_tree& tree, int host, int target,
            mm::tool::ResolvedTarget& out) {
    current_directory_guard guard;
    const mm::tool::Identity identity{"tool", false};
    return mm::tool::resolve_app_target(identity, tree.root() / "app" / "mm.mdy",
                                         host, target, "app", out);
}

void cli_status_maps_help_and_ok_to_exit_ok_and_usage_to_exit_usage() {
    mm::test::expect(mm::tool::cli_status(mm::app::Cli::ok) == mm::build::exit_ok,
                     "expected ok to map to exit_ok");
    mm::test::expect(mm::tool::cli_status(mm::app::Cli::help) == mm::build::exit_ok,
                     "expected help to map to exit_ok");
    mm::test::expect(mm::tool::cli_status(mm::app::Cli::usage) == mm::build::exit_usage,
                     "expected usage to map to exit_usage");
    mm::test::expect(mm::tool::cli_status(mm::app::Cli::manifest) ==
                         mm::build::exit_manifest,
                     "expected manifest to map to exit_manifest");
}

void manifest_status_maps_usage_to_exit_usage_and_manifest_to_exit_manifest() {
    mm::test::expect(mm::tool::manifest_status(mm::app::Cli::ok) == mm::build::exit_ok,
                     "expected ok to map to exit_ok");
    mm::test::expect(mm::tool::manifest_status(mm::app::Cli::usage) ==
                         mm::build::exit_usage,
                     "expected usage to map to exit_usage");
    mm::test::expect(mm::tool::manifest_status(mm::app::Cli::manifest) ==
                         mm::build::exit_manifest,
                     "expected manifest to map to exit_manifest");
    mm::test::expect(mm::tool::manifest_status(mm::app::Cli::help) ==
                         mm::build::exit_manifest,
                     "expected help after open_manifest to map to exit_manifest");
}

void run_status_maps_negative_to_exit_run_and_passes_statuses_through() {
    mm::test::expect(mm::tool::run_status(0) == 0, "expected zero to pass through");
    mm::test::expect(mm::tool::run_status(1) == 1, "expected one to pass through");
    mm::test::expect(mm::tool::run_status(80) == 80,
                     "expected compile failures to pass through");
    mm::test::expect(mm::tool::run_status(-1) == mm::build::exit_run,
                     "expected a negative status to map to exit_run");
}

void banner_prints_the_fixed_header() {
    std::ostringstream out;
    mm::tool::banner(out, "widget", "/fixture root");
    mm::test::expect(out.str() == "modules.cpp widget tool\n  root /fixture root\n\n",
                     "expected the exact three-line banner");
}

void lane_error_rejects_mutual_exclusion_and_repeated_flags() {
    const auto host_twice = mm::tool::lane_error(2, 0);
    mm::test::expect(host_twice && *host_twice == "lane option may be given only once",
                     "expected a repeated --host to be a usage error");
    const auto target_twice = mm::tool::lane_error(0, 2);
    mm::test::expect(target_twice && *target_twice == "lane option may be given only once",
                     "expected a repeated --target to be a usage error");
    const auto both = mm::tool::lane_error(1, 1);
    mm::test::expect(both && *both == "--host and --target are mutually exclusive",
                     "expected both flags to be a usage error");
    mm::test::expect(!mm::tool::lane_error(0, 0), "expected no flags to be accepted");
    mm::test::expect(!mm::tool::lane_error(1, 0), "expected --host alone to be accepted");
    mm::test::expect(!mm::tool::lane_error(0, 1), "expected --target alone to be accepted");
}

void select_lane_follows_flag_then_configuration() {
    mm::test::expect(
        mm::tool::select_lane(false, true, false),
        "expected the target flag to select the target lane");
    mm::test::expect(
        mm::tool::select_lane(false, true, true),
        "expected the target flag to win over a cross-selecting configuration");
    mm::test::expect(
        !mm::tool::select_lane(true, false, true),
        "expected the host flag to win over a cross-selecting configuration");
    mm::test::expect(!mm::tool::select_lane(true, false, false),
                     "expected the host flag to select the host lane");
    mm::test::expect(
        mm::tool::select_lane(false, false, true),
        "expected no flags to follow a cross-selecting configuration");
    mm::test::expect(!mm::tool::select_lane(false, false, false),
                     "expected no flags to follow a host-selecting configuration");
}

void resolve_app_target_resolves_a_built_host_app() {
    const mm::test::scoped_tree tree{"mm_tool_built"};
    write_fixture(tree, true, true);
    mm::tool::ResolvedTarget out;
    const int status = resolve(tree, 0, 0, out);
    mm::test::expect(status == mm::build::exit_ok,
                     "expected a built host app to resolve, got " + std::to_string(status));
    if (status != mm::build::exit_ok) return;
    mm::test::expect(!out.target_lane, "expected the host lane");
    mm::test::expect(
        out.node != mm::build::no_target && out.project.nodes[out.node].kind == "app",
        "expected the app node");
    mm::test::expect(
        out.executable_path ==
            std::filesystem::weakly_canonical(tree.root() / "out" / "app" / "widget"),
        "expected the built executable path");
}

void resolve_app_target_rejects_a_directory_that_is_not_an_app() {
    const mm::test::scoped_tree tree{"mm_tool_module"};
    write_fixture(tree, false, true);
    mm::tool::ResolvedTarget out;
    const int status = resolve(tree, 0, 0, out);
    mm::test::expect(status == mm::build::exit_manifest,
                     "expected a kind: module directory to be rejected, got " +
                         std::to_string(status));
}

void resolve_app_target_requires_the_executable_to_be_built() {
    const mm::test::scoped_tree tree{"mm_tool_unbuilt"};
    write_fixture(tree, true, false);
    mm::tool::ResolvedTarget out;
    const int status = resolve(tree, 0, 0, out);
    mm::test::expect(status == mm::build::exit_run,
                     "expected a missing executable to be rejected, got " +
                         std::to_string(status));
}

void resolve_app_target_rejects_a_target_lane_without_a_configured_cross() {
    const mm::test::scoped_tree tree{"mm_tool_cross"};
    write_fixture(tree, true, true);
    mm::tool::ResolvedTarget out;
    const int status = resolve(tree, 0, 1, out);
    mm::test::expect(status == mm::build::exit_manifest,
                     "expected --target on a host-only configuration to be rejected, got " +
                         std::to_string(status));
}

const mm::test::case_ cases[] = {
    {"cli_status maps help and ok to exit_ok and usage to exit_usage",
     cli_status_maps_help_and_ok_to_exit_ok_and_usage_to_exit_usage},
    {"manifest_status maps usage to exit_usage and manifest to exit_manifest",
     manifest_status_maps_usage_to_exit_usage_and_manifest_to_exit_manifest},
    {"run_status maps negative to exit_run and passes statuses through",
     run_status_maps_negative_to_exit_run_and_passes_statuses_through},
    {"banner prints the fixed header", banner_prints_the_fixed_header},
    {"lane_error rejects mutual exclusion and repeated flags",
     lane_error_rejects_mutual_exclusion_and_repeated_flags},
    {"select_lane follows flag then configuration", select_lane_follows_flag_then_configuration},
    {"resolve_app_target resolves a built host app",
     resolve_app_target_resolves_a_built_host_app},
    {"resolve_app_target rejects a directory that is not an app",
     resolve_app_target_rejects_a_directory_that_is_not_an_app},
    {"resolve_app_target requires the executable to be built",
     resolve_app_target_requires_the_executable_to_be_built},
    {"resolve_app_target rejects a target lane without a configured cross",
     resolve_app_target_rejects_a_target_lane_without_a_configured_cross},
};

const mm::test::registrar reg{"mm.tool", cases};

}  // namespace
