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

const std::vector<std::string>* lookup(const mm::mdy::MDYDocument& doc,
                                       std::string_view key) {
    auto it = doc.metadata.find(key);
    if (it != doc.metadata.end()) return &it->second;
    return nullptr;
}

bool path_contained_in(const std::filesystem::path& root,
                       const std::filesystem::path& path) {
    std::error_code ec;
    const auto canon_root = std::filesystem::canonical(root, ec);
    if (ec) return false;
    const auto canon_path = std::filesystem::canonical(path, ec);
    if (ec) return false;
    auto r_it = canon_root.begin();
    auto p_it = canon_path.begin();
    for (; r_it != canon_root.end() && p_it != canon_path.end();
         ++r_it, ++p_it) {
        if (*r_it != *p_it) return false;
    }
    return r_it == canon_root.end();
}

std::filesystem::path find_project_root_upward(std::filesystem::path dir) {
    std::error_code ec;
    for (; !dir.empty(); dir = dir.parent_path()) {
        const auto cand = dir / "mm.mdy";
        if (std::filesystem::exists(cand, ec)) {
            auto doc = mm::mdy::Parser::parse_file(cand);
            if (doc.status == mm::mdy::ParseStatus::Ok) {
                const auto* kind = lookup(doc, "kind");
                if (kind != nullptr && !kind->empty() &&
                    kind->front() == "project") {
                    return std::filesystem::weakly_canonical(dir, ec);
                }
            }
        }
        if (!dir.has_relative_path()) break;
    }
    return {};
}

std::filesystem::path find_self_project(const char* argv0) {
    std::error_code ec;
    if (std::filesystem::exists("/proc/self/exe", ec)) {
        auto exe = std::filesystem::canonical("/proc/self/exe", ec);
        if (!ec) {
            auto cand = exe.parent_path().parent_path();
            auto root = find_project_root_upward(cand);
            if (!root.empty()) return root;
        }
    }
    if (argv0 != nullptr && *argv0 != '\0') {
        auto exe = std::filesystem::weakly_canonical(argv0, ec);
        if (!ec) {
            auto cand = exe.parent_path().parent_path();
            auto root = find_project_root_upward(cand);
            if (!root.empty()) return root;
        }
    }
    auto cwd_proj = find_project_root_upward(std::filesystem::current_path());
    if (!cwd_proj.empty()) return cwd_proj;
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    mm::app::Options options("sketch");
    options.flag("--check");
    options.option("--project", "DIR");
    options.help("sketch [-v|--verbose] [-h|--help] [--check] "
                 "[--project DIR] [directory]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return 0;
    if (cli != mm::app::Cli::ok) return 64;

    const bool check_mode = options.seen("--check");
    std::filesystem::path dir = options.positional().empty()
                                    ? std::filesystem::path(".")
                                    : std::filesystem::path(options.positional().front());

    std::error_code ec;
    dir = dir.lexically_normal();
    if (dir.filename().empty() && dir != ".") {
        dir = dir.parent_path();
    }
    std::string dir_name = dir.filename().string();
    if (dir_name.empty() || dir_name == ".") {
        dir_name = std::filesystem::current_path().filename().string();
    }

    const auto abs_dir = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        std::cerr << "sketch: cannot resolve directory: " << dir.string()
                  << "\n";
        return 65;
    }

    // 1. Read the directory's own manifest first if present
    const auto manifest_path = abs_dir / "mm.mdy";
    const bool manifest_exists = std::filesystem::exists(manifest_path, ec);
    mm::mdy::MDYDocument self_doc;
    bool self_has_project = false;
    std::string self_project_value;
    if (manifest_exists) {
        self_doc = mm::mdy::Parser::parse_file(manifest_path);
        if (self_doc.status != mm::mdy::ParseStatus::Ok) {
            std::cerr << "sketch: cannot parse manifest: "
                      << manifest_path.string() << "\n";
            return 65;
        }
        const auto* proj = lookup(self_doc, "project");
        if (proj != nullptr && !proj->empty()) {
            self_has_project = true;
            self_project_value = proj->front();
        }
    }

    // 2. Check parent manifest registration
    std::string parent_msg;
    bool parent_registers_child = false;
    std::filesystem::path parent_dir = abs_dir.parent_path();
    if (parent_dir.empty()) parent_dir = ".";
    std::filesystem::path parent_manifest = parent_dir / "mm.mdy";
    if (std::filesystem::exists(parent_manifest, ec)) {
        const auto parent_doc = mm::mdy::Parser::parse_file(parent_manifest);
        if (parent_doc.status == mm::mdy::ParseStatus::Ok) {
            const auto* folders = lookup(parent_doc, "folder");
            if (folders != nullptr) {
                for (const auto& f : *folders) {
                    if (f == dir_name) {
                        parent_registers_child = true;
                        break;
                    }
                }
            }
            if (!parent_registers_child) {
                parent_msg = parent_manifest.generic_string() +
                             " does not name " + dir_name +
                             "; add folder: " + dir_name;
            }
        }
    }

    // 3. Check ancestors for tree
    bool ancestor_has_project = false;
    bool ancestor_is_project = false;
    std::filesystem::path ancestor_external_root;
    std::filesystem::path ancestor_project_root;

    for (auto cur = abs_dir.parent_path();
         !cur.empty() && cur != cur.parent_path();
         cur = cur.parent_path()) {
        const auto cand = cur / "mm.mdy";
        if (std::filesystem::exists(cand, ec)) {
            const auto doc = mm::mdy::Parser::parse_file(cand);
            if (doc.status == mm::mdy::ParseStatus::Ok) {
                const auto* p = lookup(doc, "project");
                if (p != nullptr && !p->empty()) {
                    ancestor_has_project = true;
                    if (ancestor_external_root.empty())
                        ancestor_external_root = cur;
                    break;
                }
                const auto* k = lookup(doc, "kind");
                if (k != nullptr && !k->empty() && k->front() == "project") {
                    ancestor_is_project = true;
                    if (ancestor_project_root.empty())
                        ancestor_project_root = cur;
                    break;
                }
            }
        }
    }
    const bool tree_above = ancestor_has_project || ancestor_is_project;

    if (options.seen("--project") && tree_above) {
        std::cerr << "sketch: --project is not allowed when a tree is above"
                  << " the directory\n";
        return 65;
    }

    std::filesystem::path project_root;
    if (options.seen("--project")) {
        project_root =
            std::filesystem::weakly_canonical(options.value("--project"), ec);
        if (ec || !std::filesystem::exists(project_root / "mm.mdy", ec)) {
            std::cerr << "sketch: --project does not point to a project"
                      << " directory: "
                      << options.value("--project") << "\n";
            return 65;
        }
        const auto pdoc = mm::mdy::Parser::parse_file(project_root / "mm.mdy");
        const auto* k = lookup(pdoc, "kind");
        if (k == nullptr || k->empty() || k->front() != "project") {
            std::cerr << "sketch: --project does not point to a kind: project"
                      << " manifest: "
                      << options.value("--project") << "\n";
            return 65;
        }
    } else if (ancestor_is_project) {
        project_root = ancestor_project_root;
    } else if (ancestor_has_project) {
        const auto doc =
            mm::mdy::Parser::parse_file(ancestor_external_root / "mm.mdy");
        const auto* p = lookup(doc, "project");
        if (p != nullptr && !p->empty()) {
            project_root = std::filesystem::weakly_canonical(
                ancestor_external_root / p->front(), ec);
        }
    } else if (self_has_project) {
        project_root = std::filesystem::weakly_canonical(
            abs_dir / self_project_value, ec);
    } else {
        project_root = find_self_project(argv[0]);
    }

    // Determine Case:
    // Case 1: self_has_project
    // Case 2: tree_above
    // Case 3: !tree_above && !manifest_exists
    // Case 4: !tree_above && manifest_exists && !self_has_project (orphan)
    if (!tree_above && manifest_exists && !self_has_project) {
        std::cerr << "sketch: " << manifest_path.string()
                  << ": sketch is neither registered by a parent nor external; "
                  << "add project: to it or generate in a new directory\n";
        return 65;
    }

    if (self_has_project && parent_registers_child && check_mode) {
        std::cerr << "sketch: manifest carries project: but is also registered "
                  << "by parent manifest: " << manifest_path.string() << "\n";
        return 65;
    }

    if (check_mode) {
        if (tree_above && !parent_msg.empty()) {
            std::cerr << parent_msg << "\n";
            return 65;
        }

        if (!manifest_exists) {
            std::cerr << "sketch: manifest not found: "
                      << manifest_path.string() << "\n";
            return 65;
        }

        std::string error;
        if (!mm::ino::check_application(abs_dir, self_doc, error)) {
            std::cerr << "sketch: " << error << "\n";
            return 65;
        }

        return 0;
    }

    // Generation mode
    std::vector<std::string> sketch_files;
    std::filesystem::path owning_tree = abs_dir;
    if (self_has_project) {
        owning_tree = abs_dir;
    } else if (ancestor_has_project) {
        owning_tree = ancestor_external_root;
    } else if (ancestor_is_project) {
        owning_tree = ancestor_project_root;
    }

    if (manifest_exists) {
        const auto* sketches = lookup(self_doc, "sketch");
        if (sketches == nullptr || sketches->empty()) {
            std::cerr << "sketch: manifest " << manifest_path.string()
                      << " has no sketch: entries\n";
            return 65;
        }

        for (const auto& s : *sketches) {
            sketch_files.push_back(s);
        }

        for (const auto& entry :
             std::filesystem::directory_iterator(abs_dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ino") {
                const std::string name = entry.path().filename().string();
                if (std::find(sketch_files.begin(), sketch_files.end(), name) ==
                    sketch_files.end()) {
                    std::cerr << "sketch: unmanifested .ino file: " << name << "\n";
                    return 65;
                }
            }
        }
    } else {
        std::string expected_ino = dir_name + ".ino";
        std::filesystem::path expected_path = abs_dir / expected_ino;
        if (!std::filesystem::exists(expected_path, ec)) {
            std::cerr << "sketch: expected " << expected_ino << " in "
                      << dir.string() << "\n";
            return 65;
        }

        for (const auto& entry :
             std::filesystem::directory_iterator(abs_dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ino") {
                const std::string name = entry.path().filename().string();
                if (name != expected_ino) {
                    std::cerr << "sketch: extra .ino file found without"
                              << " manifest: "
                              << name << "\n";
                    return 65;
                }
            }
        }

        sketch_files.push_back(expected_ino);

        // Generate initial manifest using guarded write
        std::string initial_manifest = "---\nmm: 1.3\nkind: app\nname: " +
                                       dir_name + "\n";
        if (!tree_above) {
            const auto rel_proj = std::filesystem::relative(
                project_root, abs_dir).lexically_normal().generic_string();
            initial_manifest += "project: " + rel_proj + "\n";
        }
        initial_manifest += "use: mm.sketch\nfile: main.cpp\nsketch: " +
                            expected_ino + "\n---\n";

        std::string write_err;
        if (!mm::ino::write_guarded(abs_dir, "mm.mdy", initial_manifest,
                                    write_err, "mm.mdy.tmp")) {
            std::cerr << "sketch: cannot write " << manifest_path.string()
                      << ": " << write_err << "\n";
            return 65;
        }
    }

    std::vector<mm::ino::SourceFile> sources;
    sources.reserve(sketch_files.size());
    for (const auto& file : sketch_files) {
        const std::filesystem::path raw_file(file);
        const std::filesystem::path joined_file = abs_dir / raw_file;
        if (raw_file.is_absolute() ||
            raw_file.lexically_normal().string().starts_with("..")) {
            std::cerr << "sketch: unsafe sketch path: " << file << "\n";
            return 65;
        }
        std::error_code sec;
        const auto abs_file = std::filesystem::canonical(joined_file, sec);
        if (sec) {
            std::cerr << "sketch: sketch source does not exist: "
                      << joined_file.string() << "\n";
            return 65;
        }
        if (!path_contained_in(owning_tree, abs_file)) {
            std::cerr << "sketch: sketch source outside tree: " << file << "\n";
            return 65;
        }

        std::ifstream in(abs_file);
        if (!in) {
            std::cerr << "sketch: cannot read " << abs_file.string() << "\n";
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
                      << (diag.is_warning ? "warning: " : "error: ")
                      << diag.message << "\n";
        }
        return 65;
    }

    for (const auto& diag : result.diagnostics) {
        std::cout << diag.file << ":" << diag.line << ": warning: "
                  << diag.message << "\n";
    }

    std::string main_err;
    if (!mm::ino::write_guarded(abs_dir, "main.cpp", result.output,
                                main_err, "main.cpp.tmp")) {
        std::cerr << "sketch: cannot write main.cpp: " << main_err << "\n";
        return 65;
    }

    // A sketch that exercises a library needs the header that library
    // includes. It is generated on the same terms as main.cpp: written
    // beside it, committed with it, and checked against it.
    const auto* libraries =
        manifest_exists ? lookup(self_doc, "sketch-library") : nullptr;
    if (libraries != nullptr && !libraries->empty()) {
        std::string header_err;
        if (!mm::ino::write_guarded(abs_dir, "Arduino.h",
                                    mm::ino::sketch_header(), header_err,
                                    "Arduino.h.tmp")) {
            std::cerr << "sketch: cannot write Arduino.h: " << header_err
                      << "\n";
            return 65;
        }
    }

    if (tree_above && !parent_msg.empty()) {
        std::cout << parent_msg << "\n";
    }

    if (!tree_above && !manifest_exists) {
        std::cout << "external sketch; build with "
                  << (project_root / "out/bin/build").string() << " .\n";
    }

    return 0;
}
