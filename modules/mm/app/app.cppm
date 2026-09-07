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

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

export module mm.app;

export class App {
public:
    App(int argc, char** argv);

    virtual ~App() = default;

    [[nodiscard]] virtual int run();

private:
    int argc_;
    char** argv_;
};


export namespace mm::app {

// How a command line failed, kept separate from any particular exit code so
// this module needs no dependency to report it. Callers map it onto their
// own codes, which for every current tool are mm::build::exit_usage and
// mm::build::exit_manifest.
enum class Cli { ok, help, usage, manifest };

// The message every tool prints for a second positional argument.
void unexpected_argument(std::string_view tool, std::string_view arg) {
    std::cerr << tool << ": unexpected argument: " << arg << "\n";
}

// True for the verbose flag every tool accepts.
[[nodiscard]] bool verbose_flag(std::string_view arg) {
    return arg == "-v" || arg == "--verbose";
}

// One command line, parsed once, for every tool that has one.
//
// The tools' flags have little in common beyond verbose and help, so rather than a parser
// that knows them all, each tool declares the shapes it accepts and then
// reads the results back. Four shapes cover every tool here:
//
//   flag("--tools")           present or absent
//   option("-e", "NAME=VALUE argument")   takes the next argument, repeatable
//   assigned("-o=")           the value follows the '=' in the same argument
//   positional_limit(2)       how many bare arguments are allowed, default 1
//   help("tool [options]")    enables -h and --help with this usage text
//
// An undeclared argument beginning with a dash is an unknown option and stops
// parsing. Only arguments that do not look like options can be positional.
class Options {
public:
    explicit Options(std::string_view tool) : tool_(tool) {}

    Options(const Options&) = delete;
    Options& operator=(const Options&) = delete;
    Options(Options&&) = delete;
    Options& operator=(Options&&) = delete;

    void flag(std::string_view name) { flags_.emplace_back(name); }

    void option(std::string_view name, std::string_view requires_hint = "an argument") {
        options_.emplace_back(name);
        hints_.emplace_back(requires_hint);
    }

    void assigned(std::string_view prefix) { assigned_.emplace_back(prefix); }

    // Opt in because apps/mdy already uses -h for HTML output. Tools that call
    // this accept both conventional help spellings and stop before doing work.
    void help(std::string_view usage) { usage_ = usage; }

    void positional_limit(std::size_t limit) { limit_ = limit; }

    // Opt in to a conventional -- separator. Everything after it is passed
    // through verbatim instead of being interpreted as this tool's options.
    void separator() { separator_ = true; }

    [[nodiscard]] Cli parse(int argc, char** argv);

    [[nodiscard]] bool verbose() const { return verbose_; }

    [[nodiscard]] bool seen(std::string_view name) const { return named(seen_, name); }

    [[nodiscard]] std::size_t count(std::string_view name) const {
        std::size_t result = 0;
        for (const auto& entry : seen_)
            if (entry == name) ++result;
        return result;
    }

    // Every value given for an option() or assigned() name, in order.
    [[nodiscard]] std::vector<std::string> values(std::string_view name) const {
        const auto it = values_.find(name);
        return it == values_.end() ? std::vector<std::string>{} : it->second;
    }

    // The first such value, or empty when the name was not given.
    [[nodiscard]] std::string value(std::string_view name) const {
        const auto all = values(name);
        return all.empty() ? std::string{} : all.front();
    }

    [[nodiscard]] const std::vector<std::string>& positional() const { return positional_; }
    [[nodiscard]] const std::vector<std::string>& trailing() const { return trailing_; }

private:
    [[nodiscard]] static bool named(const std::vector<std::string>& names, std::string_view arg) {
        for (const auto& name : names)
            if (name == arg) return true;
        return false;
    }

    std::string tool_;
    std::vector<std::string> flags_;
    std::vector<std::string> options_;
    std::vector<std::string> hints_;
    std::vector<std::string> assigned_;
    std::string usage_;
    std::size_t limit_ = 1;
    bool separator_ = false;

    bool verbose_ = false;
    std::vector<std::string> seen_;
    std::map<std::string, std::vector<std::string>, std::less<>> values_;
    std::vector<std::string> positional_;
    std::vector<std::string> trailing_;
};

Cli Options::parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (separator_ && arg == "--") {
            for (++i; i < argc; ++i) trailing_.emplace_back(argv[i]);
            break;
        }

        if (!usage_.empty() && (arg == "-h" || arg == "--help")) {
            std::cout << "Usage: " << usage_ << "\n";
            return Cli::help;
        }

        if (verbose_flag(arg)) {
            verbose_ = true;
            continue;
        }

        if (named(flags_, arg)) {
            seen_.emplace_back(arg);
            continue;
        }

        bool handled = false;
        for (const auto& prefix : assigned_) {
            if (arg.size() < prefix.size() || arg.substr(0, prefix.size()) != prefix) continue;
            values_[prefix].emplace_back(arg.substr(prefix.size()));
            handled = true;
            break;
        }
        if (handled) continue;

        for (std::size_t n = 0; n < options_.size(); ++n) {
            if (options_[n] != arg) continue;
            if (i + 1 >= argc) {
                std::cerr << tool_ << ": " << arg << " requires " << hints_[n] << "\n";
                return Cli::usage;
            }
            values_[options_[n]].emplace_back(argv[++i]);
            handled = true;
            break;
        }
        if (handled) continue;

        if (arg.starts_with('-')) {
            std::cerr << tool_ << ": unknown option: " << arg << "\n";
            return Cli::usage;
        }

        if (positional_.size() >= limit_) {
            unexpected_argument(tool_, arg);
            return Cli::usage;
        }
        positional_.emplace_back(arg);
    }

    return Cli::ok;
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

App::App(int argc, char** argv) : argc_(argc), argv_(argv) {}

int App::run() {
    const std::string tool = argc_ > 0 && argv_ != nullptr && argv_[0] != nullptr
                                 ? std::filesystem::path(argv_[0]).filename().string()
                                 : std::string("app");
    mm::app::Options options(tool);
    options.help(tool + " [-v|--verbose] [-h|--help]");
    const auto cli = options.parse(argc_, argv_);
    if (cli == mm::app::Cli::help) return 0;
    if (cli != mm::app::Cli::ok) return 64;

    if (options.verbose())
        std::cout << "modules.cpp " << tool << " application\n  verbose true\n";
    return 0;
}
