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
    options.flag("--library");
    options.flag("--verbose");
    options.option("--project", "DIR");
    options.help("sketch [-v|--verbose] [-h|--help] [--check] [--library] "
                 "[--project DIR] [directory]");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return 0;
    if (cli != mm::app::Cli::ok) return 64;

    const bool check_mode = options.seen("--check");
    const bool verbose = options.seen("--verbose");
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

    const bool is_library =
        options.seen("--library") || mm::ino::is_library_root(abs_dir);

    if (is_library) {
        if (tree_above && !parent_registers_child) {
            std::cerr << parent_msg << "\n";
            return 65;
        }

        const bool expect_project = !tree_above;
        const std::string project_rel =
            expect_project
                ? project_root.lexically_relative(abs_dir).generic_string()
                : "";

        auto plan = mm::ino::discover_library(abs_dir);
        if (!plan.ok) {
            std::cerr << "sketch: " << plan.error << "\n";
            return 65;
        }

        if (verbose) {
            std::cerr << "sketch: found " << plan.app_nodes.size()
                      << " app nodes, " << plan.dir_nodes.size()
                      << " dir nodes\n";
        }

        for (const auto& skip_msg : plan.skipped) {
            std::cerr << "sketch: " << skip_msg << "\n";
        }

        if (plan.app_nodes.empty()) {
            std::cerr << "sketch: no sketch applications found under "
                      << (abs_dir / "examples").string() << "\n";
            return 65;
        }

        std::vector<std::string> compat_errors;
        if (std::filesystem::exists(abs_dir / "mm.mdy", ec)) {
            const auto doc = mm::mdy::Parser::parse_file(abs_dir / "mm.mdy");
            std::string err;
            if (!mm::ino::validate_manifest_compatibility(
                    doc, &plan.root_node, nullptr, expect_project,
                    abs_dir, abs_dir / "mm.mdy", err, project_root)) {
                compat_errors.push_back(err);
            }
        }
        for (const auto& dir_node : plan.dir_nodes) {
            const auto mpath = dir_node.dir / "mm.mdy";
            if (std::filesystem::exists(mpath, ec)) {
                const auto doc = mm::mdy::Parser::parse_file(mpath);
                std::string err;
                if (!mm::ino::validate_manifest_compatibility(
                        doc, &dir_node, nullptr, false,
                        abs_dir, mpath, err)) {
                    compat_errors.push_back(err);
                }
            }
        }
        for (const auto& app_node : plan.app_nodes) {
            const auto mpath = app_node.dir / "mm.mdy";
            if (std::filesystem::exists(mpath, ec)) {
                const auto doc = mm::mdy::Parser::parse_file(mpath);
                std::string err;
                if (!mm::ino::validate_manifest_compatibility(
                        doc, nullptr, &app_node, false,
                        abs_dir, mpath, err)) {
                    compat_errors.push_back(err);
                }
            }
        }
        if (!compat_errors.empty()) {
            for (const auto& e : compat_errors) {
                std::cerr << "sketch: " << e << "\n";
            }
            return 65;
        }

        if (check_mode) {
            bool check_failed = false;
            if (!std::filesystem::exists(abs_dir / "mm.mdy", ec)) {
                std::cerr << "sketch: manifest not found: "
                          << (abs_dir / "mm.mdy").string() << "\n";
                check_failed = true;
            }
            for (const auto& dir_node : plan.dir_nodes) {
                const auto mpath = dir_node.dir / "mm.mdy";
                if (!std::filesystem::exists(mpath, ec)) {
                    std::cerr << "sketch: manifest not found: "
                              << mpath.string() << "\n";
                    check_failed = true;
                }
            }
            for (const auto& app_node : plan.app_nodes) {
                const auto mpath = app_node.dir / "mm.mdy";
                if (!std::filesystem::exists(mpath, ec)) {
                    std::cerr << "sketch: manifest not found: "
                              << mpath.string() << "\n";
                    check_failed = true;
                }
                const auto main_path = app_node.dir / "main.cpp";
                if (!std::filesystem::exists(main_path, ec)) {
                    std::cerr << "sketch: missing generated main.cpp in "
                              << app_node.dir.string() << "\n";
                    check_failed = true;
                } else {
                    std::vector<mm::ino::SourceFile> sources;
                    for (const auto& s : app_node.sketches) {
                        std::ifstream in(app_node.dir / s);
                        if (in) {
                            std::ostringstream ss;
                            ss << in.rdbuf();
                            sources.push_back({s, ss.str()});
                        }
                    }
                    const auto tr = mm::ino::transform(sources);
                    std::ifstream in_main(main_path);
                    std::ostringstream ss_main;
                    ss_main << in_main.rdbuf();
                    if (!tr.ok || ss_main.str() != tr.output) {
                        std::cerr
                            << "sketch: committed main.cpp does not match"
                               " sketch in "
                            << app_node.dir.string() << "\n";
                        check_failed = true;
                    }
                }
                const auto header_path = app_node.dir / "Arduino.h";
                if (verbose && std::filesystem::exists(header_path, ec)) {
                    std::ifstream in(header_path);
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    if (ss.str() != mm::ino::sketch_header()) {
                        std::cerr
                            << "sketch: committed Arduino.h does not match"
                               " this release in "
                            << app_node.dir.string() << "\n";
                        check_failed = true;
                    }
                } else if (!std::filesystem::exists(header_path, ec)) {
                    std::cerr << "sketch: missing generated Arduino.h in "
                              << app_node.dir.string() << "\n";
                    check_failed = true;
                } else {
                    std::ifstream in_header(header_path);
                    std::ostringstream ss_header;
                    ss_header << in_header.rdbuf();
                    if (ss_header.str() != mm::ino::sketch_header()) {
                        std::cerr
                            << "sketch: committed Arduino.h does not match"
                               " this release in "
                            << app_node.dir.string() << "\n";
                        check_failed = true;
                    }
                }
            }
            if (check_failed) return 65;
            return 0;
        }

        std::string err;
        const auto root_manifest = abs_dir / "mm.mdy";
        if (!std::filesystem::exists(root_manifest, ec)) {
            const std::string content =
                mm::ino::render_root_manifest(plan.root_node, project_rel);
            if (content.empty()) {
                std::cerr << "sketch: invalid manifest content for "
                          << root_manifest.string() << "\n";
                return 65;
            }
            if (!mm::ino::write_guarded(abs_dir, "mm.mdy", content, err,
                                        "mm.mdy.tmp")) {
                std::cerr << "sketch: cannot write " << root_manifest.string()
                          << ": " << err << "\n";
                return 65;
            }
        }
        for (const auto& dir_node : plan.dir_nodes) {
            const auto mpath = dir_node.dir / "mm.mdy";
            if (!std::filesystem::exists(mpath, ec)) {
                const std::string content =
                    mm::ino::render_dir_manifest(dir_node);
                if (content.empty()) {
                    std::cerr << "sketch: invalid manifest content for "
                              << mpath.string() << "\n";
                    return 65;
                }
                if (!mm::ino::write_guarded(dir_node.dir, "mm.mdy", content,
                                            err, "mm.mdy.tmp")) {
                    std::cerr << "sketch: cannot write " << mpath.string()
                              << ": " << err << "\n";
                    return 65;
                }
            }
        }
        for (const auto& app_node : plan.app_nodes) {
            const auto mpath = app_node.dir / "mm.mdy";
            if (!std::filesystem::exists(mpath, ec)) {
                const std::string content =
                    mm::ino::render_app_manifest(app_node);
                if (content.empty()) {
                    std::cerr << "sketch: invalid manifest content for "
                              << mpath.string() << "\n";
                    return 65;
                }
                if (!mm::ino::write_guarded(app_node.dir, "mm.mdy", content,
                                            err, "mm.mdy.tmp")) {
                    std::cerr << "sketch: cannot write " << mpath.string()
                              << ": " << err << "\n";
                    return 65;
                }
            }
            std::vector<mm::ino::SourceFile> sources;
            for (const auto& s : app_node.sketches) {
                std::ifstream in(app_node.dir / s);
                if (!in) {
                    std::cerr << "sketch: cannot open sketch file: "
                              << (app_node.dir / s).string() << "\n";
                    return 65;
                }
                std::ostringstream ss;
                ss << in.rdbuf();
                sources.push_back({s, ss.str()});
            }
            const auto tr = mm::ino::transform(sources);
            if (!tr.ok) {
                std::cerr << "sketch: transformation failed for "
                          << app_node.name << "\n";
                return 65;
            }
            if (!mm::ino::write_guarded(app_node.dir, "main.cpp", tr.output,
                                        err, "main.cpp.tmp")) {
                std::cerr << "sketch: cannot write main.cpp in "
                          << app_node.dir.string() << ": " << err << "\n";
                return 65;
            }
            if (!mm::ino::write_guarded(app_node.dir, "Arduino.h",
                                        mm::ino::sketch_header(), err,
                                        "Arduino.h.tmp")) {
                std::cerr << "sketch: cannot write Arduino.h in "
                          << app_node.dir.string() << ": " << err << "\n";
                return 65;
            }
        }
        if (!tree_above) {
            if (verbose) {
                std::cerr << "sketch: external sketch detected\n";
            }
            std::cout << "external sketch; build with "
                      << (project_root / "out/bin/build").string() << " "
                      << abs_dir.string() << "\n";
        }
        return 0;
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
        if (verbose) {
            std::cerr << "sketch: manifest carries project: but is also registered "
                      << "by parent manifest: " << manifest_path.string() << "\n";
        }
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

    if (verbose) {
        std::cerr << "sketch: processing directory: " << abs_dir.string() << "\n";
        std::cerr << "sketch: owning tree: " << owning_tree.string() << "\n";
    }

    if (manifest_exists) {
        const auto* sketches = lookup(self_doc, "sketch");
        if (sketches == nullptr || sketches->empty()) {
            std::cerr << "sketch: manifest " << manifest_path.string()
                      << " has no sketch: entries\n";
            return 65;
        }

        for (const auto& s : *sketches) {
            if (verbose) {
                std::cerr << "sketch: manifest entry: " << s << "\n";
            }
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
        std::cerr << "sketch: transformation failed\n";
        for (const auto& diag : result.diagnostics) {
            std::cerr << diag.file << ":" << diag.line << ": "
                      << (diag.is_warning ? "warning: " : "error: ")
                      << diag.message << "\n";
        }
        return 65;
    }

    if (verbose) {
        std::cerr << "sketch: transformed " << sketch_files.size()
                  << " source file(s)\n";
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

    if (verbose) {
        std::cerr << "sketch: wrote main.cpp to " << abs_dir.string()
                  << "/main.cpp\n";
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
        if (verbose) {
            std::cerr << "sketch: wrote Arduino.h to " << abs_dir.string()
                      << "/Arduino.h\n";
        }
    }

    if (tree_above && !parent_msg.empty()) {
        std::cout << parent_msg << "\n";
    }

    if (!tree_above && !manifest_exists) {
        std::cout << "external sketch; build with "
                  << (project_root / "out/bin/build").string() << " .\n";
    }

    if (verbose) {
        std::cerr << "sketch: done\n";
    }

    return 0;
}
