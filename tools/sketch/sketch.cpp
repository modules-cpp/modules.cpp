// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

import mm.app;
import mm.mdy;
import mm.ino;

namespace {
const std::vector<std::string>* lookup(const mm::mdy::MDYDocument& doc, std::string_view key) {
    auto it = doc.metadata.find(key);
    if (it != doc.metadata.end()) return &it->second;
    return nullptr;
}
}

int main(int argc, char** argv) {
    mm::app::Options options("sketch");
    options.flag("--check");
    options.help("sketch [--check] [directory]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return 0;
    if (cli != mm::app::Cli::ok) return 64;

    const bool check_mode = options.seen("--check");
    std::filesystem::path dir = options.positional().empty()
                                    ? std::filesystem::path(".")
                                    : std::filesystem::path(options.positional().front());

    // Normalize trailing slash so filename() is the directory name
    std::error_code ec;
    dir = dir.lexically_normal();
    if (dir.filename().empty() && dir != ".") {
        dir = dir.parent_path();
    }
    std::string dir_name = dir.filename().string();
    if (dir_name.empty() || dir_name == ".") {
        dir_name = std::filesystem::current_path().filename().string();
    }

    // Check parent manifest registration
    std::string parent_msg;
    std::filesystem::path parent_dir = dir.parent_path();
    if (parent_dir.empty()) parent_dir = ".";
    std::filesystem::path parent_manifest = parent_dir / "mm.mdy";
    if (std::filesystem::exists(parent_manifest)) {
        const auto parent_doc = mm::mdy::Parser::parse_file(parent_manifest);
        if (parent_doc.status == mm::mdy::ParseStatus::Ok) {
            const auto* folders = lookup(parent_doc, "folder");
            bool found_folder = false;
            if (folders != nullptr) {
                for (const auto& f : *folders) {
                    if (f == dir_name) {
                        found_folder = true;
                        break;
                    }
                }
            }
            if (!found_folder) {
                parent_msg = parent_manifest.generic_string() + " does not name " + dir_name + "; add folder: " + dir_name;
            }
        }
    }

    if (check_mode) {
        if (!parent_msg.empty()) {
            std::cerr << parent_msg << "\n";
            return 65;
        }

        std::filesystem::path manifest_path = dir / "mm.mdy";
        if (!std::filesystem::exists(manifest_path)) {
            std::cerr << "sketch: manifest not found: " << manifest_path.string() << "\n";
            return 65;
        }

        const auto doc = mm::mdy::Parser::parse_file(manifest_path);
        if (doc.status != mm::mdy::ParseStatus::Ok) {
            std::cerr << "sketch: cannot parse manifest: " << manifest_path.string() << "\n";
            return 65;
        }

        std::string error;
        if (!mm::ino::check_application(dir, doc, error)) {
            std::cerr << "sketch: " << error << "\n";
            return 65;
        }

        return 0;
    }

    // Generation mode
    std::filesystem::path manifest_path = dir / "mm.mdy";
    std::vector<std::string> sketch_files;

    if (std::filesystem::exists(manifest_path)) {
        const auto doc = mm::mdy::Parser::parse_file(manifest_path);
        if (doc.status != mm::mdy::ParseStatus::Ok) {
            std::cerr << "sketch: cannot parse manifest: " << manifest_path.string() << "\n";
            return 65;
        }

        const auto* sketches = lookup(doc, "sketch");
        if (sketches == nullptr || sketches->empty()) {
            std::cerr << "sketch: manifest " << manifest_path.string() << " has no sketch: entries\n";
            return 65;
        }

        for (const auto& s : *sketches) {
            sketch_files.push_back(s);
        }

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ino") {
                const std::string name = entry.path().filename().string();
                if (std::find(sketch_files.begin(), sketch_files.end(), name) == sketch_files.end()) {
                    std::cerr << "sketch: unmanifested .ino file: " << name << "\n";
                    return 65;
                }
            }
        }
    } else {
        std::string expected_ino = dir_name + ".ino";
        std::filesystem::path expected_path = dir / expected_ino;
        if (!std::filesystem::exists(expected_path)) {
            std::cerr << "sketch: expected " << expected_ino << " in " << dir.string() << "\n";
            return 65;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ino") {
                const std::string name = entry.path().filename().string();
                if (name != expected_ino) {
                    std::cerr << "sketch: extra .ino file found without manifest: " << name << "\n";
                    return 65;
                }
            }
        }

        sketch_files.push_back(expected_ino);

        // Generate initial manifest
        std::ofstream out_manifest(manifest_path);
        if (!out_manifest) {
            std::cerr << "sketch: cannot write " << manifest_path.string() << "\n";
            return 65;
        }
        out_manifest << "---\n"
                     << "mm: 1.3\n"
                     << "kind: app\n"
                     << "name: " << dir_name << "\n"
                     << "use: mm.sketch\n"
                     << "file: main.cpp\n"
                     << "sketch: " << expected_ino << "\n"
                     << "---\n";
    }

    std::vector<mm::ino::SourceFile> sources;
    sources.reserve(sketch_files.size());
    for (const auto& file : sketch_files) {
        std::filesystem::path p = dir / file;
        std::ifstream in(p);
        if (!in) {
            std::cerr << "sketch: cannot read " << p.string() << "\n";
            return 65;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        sources.push_back({file, ss.str()});
    }

    const auto result = mm::ino::transform(sources);
    if (!result.ok) {
        for (const auto& diag : result.diagnostics) {
            std::cerr << diag.file << ":" << diag.line << ": "
                      << (diag.is_warning ? "warning: " : "error: ") << diag.message << "\n";
        }
        return 65;
    }

    for (const auto& diag : result.diagnostics) {
        std::cout << diag.file << ":" << diag.line << ": warning: " << diag.message << "\n";
    }

    std::filesystem::path main_path = dir / "main.cpp";
    std::ofstream out_main(main_path);
    if (!out_main) {
        std::cerr << "sketch: cannot write " << main_path.string() << "\n";
        return 65;
    }
    out_main << result.output;

    if (!parent_msg.empty()) {
        std::cout << parent_msg << "\n";
    }

    return 0;
}
