// The shared front-end prologue for the lane tools: it locates one
// already-registered node in the project and decides whether that node is
// available in the selected lane.
//
// run, debug, flash, and test each resolved the same root, configuration,
// lane, project, staleness, node, and availability sequence before doing
// anything tool-specific. setup() performs that sequence once; the front
// ends keep their option parsing, their domain checks (runner, debugger,
// board support, test runner), and their epilogues.
//
// mm.tool is the thin seam between the front ends and the build engine: it
// owns no policy of its own and defers every rule to mm.app and mm.build.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module mm.tool;

import mm.app;
import mm.build;

export namespace mm::tool {

// The located node in the selected lane, plus everything the front end's
// epilogue consumes. ok is false and status is the exit code the front end
// should return when setup() did not complete; the reason is already printed
// under the tool name, except where the shared step printed its own
// diagnostic and status stands alone.
struct Setup {
    bool ok = true;
    int status = mm::build::exit_ok;

    mm::build::ResolvedRoots roots;
    mm::build::BuildConfiguration configuration;
    bool target_lane = false;
    mm::build::Toolchain toolchain;        // valid when ok
    std::filesystem::path lane_directory;  // valid when ok
    std::optional<mm::build::Platform> platform; // nullopt: target lane with
                                                 // no configured platform
    mm::build::Project project;
    std::vector<bool> buildable;
    std::size_t node = mm::build::no_target;
    std::optional<mm::build::PlatformProviders> providers;
    std::filesystem::path context_tree_root;
    std::filesystem::path context_output_root;

    // The in-lane artifact root, paired with the tools directory and the
    // external flag, for the front end's ArtifactContext.
    [[nodiscard]] mm::build::ArtifactContext artifact_context() const {
        return mm::build::ArtifactContext(context_tree_root, context_output_root,
                                           roots.tools_dir,
                                           roots.external_root.has_value());
    }
};

// Which shared setup one front end wants.
struct Options {
    std::string_view tool;
    std::filesystem::path manifest;  // the resolved manifest path
    bool host = false;               // --host was given
    bool target = false;             // --target was given
    bool force_target_lane = false;  // flash: always the target lane
    bool verbose = false;
    std::string_view node_kind;      // empty means no kind check
    bool match_by_manifest_path = false;  // canonical manifest match, not
                                          // source directory (test)
    bool reject_external_root = false;    // an external root holds no test
    bool resolve_providers = false;       // resolve platform_providers too
    bool check_providers_ok = false;      // fail on an invalid provider analysis
    std::string_view lane_not_configured = "target lane is not configured";
    std::string_view external_root_rejection = "external root holds no test";
};

// Resolves the manifest's project root, enters it, loads the configuration,
// selects the lane and platform, loads the project, checks configuration
// staleness, locates the requested node, and decides its availability in the
// selected lane.
[[nodiscard]] Setup setup(const Options& options);
}
