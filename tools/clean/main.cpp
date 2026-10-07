// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <iostream>
#include <string>
import mm.app;
import mm.build;
import mm.mdy;

int main(int argc, char** argv) {
    mm::app::Options options("clean");
    options.flag("--host");
    options.flag("--target");
    options.flag("--distclean");
    options.help("clean [--host | --target] [--distclean] [manifest]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok || options.positional().size() > 1 ||
        (options.seen("--host") && options.seen("--target")) ||
        (options.seen("--distclean") && (options.seen("--host") || options.seen("--target"))))
        return mm::build::exit_usage;
    const auto manifest = mm::build::resolve_manifest(options.positional().empty()
        ? std::filesystem::path(".") : std::filesystem::path(options.positional().front()));
    const auto doc = mm::mdy::Parser::parse_file(manifest);
    const auto kind = doc.metadata.find("kind");
    if (kind != doc.metadata.end() && kind->second.size() == 1 && kind->second.front() == "project") {
        // Keep the historic project-root full bootstrap cleanup.
        if (options.seen("--host") || options.seen("--target")) return mm::build::exit_usage;
        std::error_code ec;
        const auto root = std::filesystem::canonical(std::filesystem::absolute(manifest).parent_path(), ec);
        if (ec) return mm::build::exit_manifest;
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            const auto name = entry.path().filename().string();
            if (name == "out" || name.starts_with("out-") || name == "gcm.cache" || name == "help-dummy.o") {
                std::filesystem::remove_all(entry.path(), ec);
                if (ec) return mm::build::exit_manifest;
            }
        }
        return mm::build::exit_ok;
    }
    return mm::build::clean_external(manifest, options.seen("--host"), options.seen("--target"), options.seen("--distclean"));
}
