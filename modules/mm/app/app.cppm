// The application boundary: the App base class every program can build on,
// and the command line handling those programs would otherwise each repeat.
//
// mm.app imports nothing, and must keep it that way. It is the most
// foundational module here, and apps/main exists to show that a program can
// be built from it alone; importing mm.build would pull the manifest parser
// and the whole build graph into that program. That constraint is also why
// the manifest helpers below take an already resolved path and report a
// status rather than an exit code: resolving a directory to its mm.mdy is
// mm::build::resolve_manifest's job, and the exit codes live in mm.build
// with the compile, link, and run codes they belong beside. Callers pair
// the two, which costs a line and keeps this module free of both.
//
// tools/build deliberately does not use any of this. It is compiled by a
// fixed file list in bootstrap.sh and in build0, and adding mm.app to that
// list would grow the minimal bootstrap set; see docs/modules.mdy.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <iostream>
#include <string_view>

export module mm.app;

export class App {
public:
    App(int argc, char** argv);

    virtual ~App() = default;

    [[nodiscard]] virtual int run() {return 0;};

    [[nodiscard]] int main(int argc, char** argv) {
        return this->run();
    }
};


App::App(int argc, char** argv)
{

}

export namespace mm::app {

// How a command line failed, kept separate from any particular exit code so
// this module needs no dependency to report it. Callers map it onto their
// own codes, which for every current tool are mm::build::exit_usage and
// mm::build::exit_manifest.
enum class Cli { ok, usage, manifest };

// True for the verbose flag every tool accepts. Kept as a predicate rather
// than a whole parser because the tools genuinely differ around it: mdy also
// takes -s, -h and -o=, model takes --configuration and --tools, and shell
// takes a repeatable -e and up to two positionals. Each keeps its own loop
// and calls this for the one flag they all share.
[[nodiscard]] bool verbose_flag(std::string_view arg) {
    return arg == "-v" || arg == "--verbose";
}

// The message every tool prints for a second positional argument.
void unexpected_argument(std::string_view tool, std::string_view arg) {
    std::cerr << tool << ": unexpected argument: " << arg << "\n";
}

// Validates a manifest path that has already been resolved (see
// mm::build::resolve_manifest), sets root to the directory holding it, and
// enters that directory when enter_root is true. Reports the reason on
// stderr, prefixed with tool, and returns what kind of failure it was.
[[nodiscard]] Cli open_manifest(std::string_view tool, const std::filesystem::path& manifest,
                                std::filesystem::path& root, bool enter_root) {
    if (manifest.filename() != "mm.mdy") {
        std::cerr << tool << ": not an mm.mdy manifest: " << manifest.string() << "\n";
        return Cli::usage;
    }

    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec) || ec) {
        std::cerr << tool << ": manifest does not exist: " << manifest.string() << "\n";
        return Cli::manifest;
    }

    root = std::filesystem::absolute(manifest, ec).parent_path();
    if (ec) {
        std::cerr << tool << ": cannot resolve " << manifest.string() << ": " << ec.message()
                  << "\n";
        return Cli::manifest;
    }

    if (enter_root) {
        std::filesystem::current_path(root, ec);
        if (ec) {
            std::cerr << tool << ": cannot enter " << root.string() << ": " << ec.message()
                      << "\n";
            return Cli::manifest;
        }
    }

    return Cli::ok;
}

}  // namespace mm::app
