// The shared front end protocol for the command-line tools in tools/.
//
// mm.tool sits above mm.app and mm.build and below the domain modules
// (mm.run, mm.debug, mm.flash, ...). A tool's main becomes parse plus
// mm::tool::resolve_app_target plus the domain execute() call.
//
// tools/build deliberately does not use any of this. It is compiled by the
// fixed build1 file list in bootstrap.sh before any manifest exists, so
// mm.app (and with it mm.tool) enters bootstrap only through the configure
// manifest's ordinary dependency closure; see docs/modules.mdy.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

export module mm.tool;

import mm.app;
import mm.build;

export namespace mm::tool {

// One identity per tool process: the same string that is today written
// separately into Options, open_manifest, LoadPolicy.tool,
// log_configuration, check_configuration_staleness, and the banner.
struct Identity {
    std::string_view name;
    bool verbose;
};

// The parse preamble mapping: ok and help -> exit_ok, usage -> exit_usage.
[[nodiscard]] int cli_status(mm::app::Cli status);

// The open_manifest mapping: ok -> exit_ok, usage -> exit_usage, and any
// other status -> exit_manifest.
[[nodiscard]] int manifest_status(mm::app::Cli status);

// The repeated banner, written to the given stream (std::cout in tools).
void banner(std::ostream& out, std::string_view name, const std::filesystem::path& root);

// The repeated trailing map: negative -> exit_run, otherwise the status.
[[nodiscard]] int run_status(int status);

// The repeated lane-flag validation, pure so it is testable without a
// configuration: both flags given, or either flag repeated, is a usage
// error (returns the error text); otherwise nullopt.
[[nodiscard]] std::optional<std::string> lane_error(
    int host_count, int target_count);

// The repeated lane derivation, pure: the target flag wins, the host flag
// forces the host lane, and with neither the configuration's selection
// decides.
[[nodiscard]] bool select_lane(bool host_flag, bool target_flag,
                               bool configuration_selects_cross);

// Everything a domain execute() needs after the shared preamble, mirroring
// the locals of tools/run/main.cpp. The project is held by value so node
// indices and any pointers into it stay valid for the caller.
struct ResolvedTarget {
    mm::build::BuildConfiguration configuration;
    bool target_lane;
    mm::build::Project project;
    std::size_t node;                     // the requested node's index
    std::filesystem::path executable_path;
    mm::build::ArtifactContext context;
};

// The shared resolve protocol (run/debug/flash preamble, written once):
// lane validation -> open_manifest -> resolve_roots -> enter project root ->
// resolve_configuration -> lane selection -> toolchain and lane directory
// null checks -> load_project -> check_configuration_staleness -> node
// lookup by requested directory (kind must equal node_kind, "app" or
// "test") -> resolve_structural_properties -> availability -> ArtifactContext
// and executable path -> existence check.
//
// On failure prints "<name>: <message>" on stderr and returns the
// appropriate mm::build::exit_* code; open_manifest and
// resolve_configuration report their own failures, which this maps on. On
// success fills out and returns exit_ok. The failure messages ("not a
// registered app", "target lane is not configured", "not built; run build
// first") live here only.
[[nodiscard]] int resolve_app_target(
    const Identity&,
    const std::filesystem::path& manifest,
    int host_count,
    int target_count,
    std::string_view node_kind,
    ResolvedTarget& out);

}  // namespace mm::tool
