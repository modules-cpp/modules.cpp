// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// Tests for external application grafting and artifact contexts.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import mm.build;
import mm.configure;
import mm.test;

namespace {

using mm::test::expect;

void app_root_grafting() {
    const mm::test::scoped_tree proj_tree{"graft_proj_app"};
    proj_tree.manifest("", "kind: project\nname: proj\nfolder: modules\n");
    proj_tree.manifest("modules", "kind: dir\nname: modules\nfolder: m\n");
    proj_tree.manifest("modules/m",
                       "kind: module\nname: m\nmodule: mm.m\nfile: m.cppm\n");
    std::ofstream(proj_tree.root() / "modules/m/m.cppm")
        << "export module mm.m;\n";

    const mm::test::scoped_tree ext_tree{"graft_ext_app"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: ext_blink\nproject: " + rel_proj +
        "\nuse: mm.m\nfile: main.cpp\nsketch: blink.ino\n");
    std::ofstream(ext_tree.root() / "main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "blink.ino")
        << "void setup(){}\nvoid loop(){}\n";

    mm::build::LoadPolicy policy{.tool = "build", .external = ext_tree.root()};
    const auto project = mm::build::load_project(proj_tree.root(), policy);
    expect(project.ok, "load_project with external app succeeds");

    // proj(0), modules(1), m(2), ext_blink(3)
    expect(project.nodes.size() == 4, "external root grafted as 4th node");
    const auto& ext_node = project.nodes.back();
    expect(ext_node.name == "ext_blink", "grafted node name matches");
    expect(ext_node.parent == 0, "grafted node parent is project root (0)");
    expect(ext_node.external, "grafted node is marked external");
    expect(ext_node.non_core, "grafted node is marked non-core");
    expect(ext_node.source_dir == ext_tree.root(),
           "source_dir is absolute external root");
}

void dir_root_grafting() {
    const mm::test::scoped_tree proj_tree{"graft_proj_dir"};
    proj_tree.manifest("", "kind: project\nname: proj\n");

    const mm::test::scoped_tree ext_tree{"graft_ext_dir"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: dir\nname: sketches\nproject: " + rel_proj +
        "\nfolder: blink\nfolder: button\n");

    ext_tree.manifest_raw("blink",
        "mm: 1.3\nkind: app\nname: blink\nfile: main.cpp\nsketch: blink.ino\n");
    std::filesystem::create_directories(ext_tree.root() / "blink");
    std::ofstream(ext_tree.root() / "blink/main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "blink/blink.ino") << "void setup(){}\n";

    ext_tree.manifest_raw("button",
        "mm: 1.3\nkind: app\nname: button\nfile: main.cpp\n"
        "sketch: button.ino\n");
    std::filesystem::create_directories(ext_tree.root() / "button");
    std::ofstream(ext_tree.root() / "button/main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "button/button.ino") << "void setup(){}\n";

    mm::build::LoadPolicy policy{.tool = "build", .external = ext_tree.root()};
    const auto project = mm::build::load_project(proj_tree.root(), policy);
    expect(project.ok, "load_project with external dir succeeds");

    // proj(0), sketches(1), blink(2), button(3)
    expect(project.nodes.size() == 4, "external dir and children grafted");
    expect(project.nodes[1].name == "sketches" && project.nodes[1].parent == 0,
           "sketches dir parent is root (0)");
    expect(project.nodes[1].external && project.nodes[1].non_core,
           "sketches dir is external and non-core");
    expect(project.nodes[2].name == "blink" && project.nodes[2].parent == 1,
           "blink parent is sketches (1)");
    expect(project.nodes[3].name == "button" && project.nodes[3].parent == 1,
           "button parent is sketches (1)");
    expect(project.nodes[2].external && project.nodes[3].external,
           "children are marked external");
}

void graft_allowlist_refusals() {
    const mm::test::scoped_tree proj_tree{"graft_proj_allow"};
    proj_tree.manifest("", "kind: project\nname: proj\n");

    const mm::test::scoped_tree ext_tree{"graft_ext_allow"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();

    const std::vector<std::pair<std::string, std::string>> forbidden_kinds = {
        {"module", "mm: 1.3\nkind: module\nname: bad_mod\n"
                   "module: mm.bad\nfile: m.cppm\n"},
        {"library", "mm: 1.3\nkind: library\nname: bad_lib\n"
                    "source: src\nlicence: LIC\n"},
        {"test", "mm: 1.3\nkind: test\nname: bad_test\nunit: t.cpp\n"},
        {"doc", "mm: 1.3\nkind: doc\nname: bad_doc\nfile: doc.mdy\n"},
        {"board", "mm: 1.3\nkind: board\nname: bad_board\nderives-from: b\n"},
        {"sdk", "mm: 1.3\nkind: sdk\nname: bad_sdk\nlibrary: l\n"},
        {"app_no_sketch",
         "mm: 1.3\nkind: app\nname: no_sketch\nfile: main.cpp\n"},
    };

    for (const auto& [tag, content] : forbidden_kinds) {
        ext_tree.manifest_raw("",
            "mm: 1.3\nkind: dir\nname: sketches\nproject: " + rel_proj +
            "\nfolder: child\n");
        ext_tree.manifest_raw("child", content);
        std::ofstream(ext_tree.root() / "child/main.cpp") << "int main(){}\n";
        std::ofstream(ext_tree.root() / "child/m.cppm")
            << "export module mm.bad;\n";
        std::ofstream(ext_tree.root() / "child/t.cpp") << "int main(){}\n";
        std::ofstream(ext_tree.root() / "child/doc.mdy") << "# Doc\n";
        std::ofstream(ext_tree.root() / "child/LIC") << "mit\n";
        std::filesystem::create_directories(ext_tree.root() / "child/src");

        mm::build::LoadPolicy policy{.tool = "build",
                                     .external = ext_tree.root()};
        const auto project = mm::build::load_project(proj_tree.root(), policy);
        expect(!project.ok, "external graft allowlist refuses kind: " + tag);
    }
}

void uniqueness_rules() {
    const mm::test::scoped_tree proj_tree{"graft_proj_unique"};
    proj_tree.manifest("", "kind: project\nname: proj\nfolder: blink\n");
    proj_tree.manifest("blink", "kind: app\nname: blink\nfile: main.cpp\n");
    std::ofstream(proj_tree.root() / "blink/main.cpp") << "int main(){}\n";

    const mm::test::scoped_tree ext_tree{"graft_ext_unique"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();

    // 1. Cross-tree name sharing: external 'blink' beside project 'blink' loads
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: blink\nproject: " + rel_proj +
        "\nfile: main.cpp\nsketch: blink.ino\n");
    std::ofstream(ext_tree.root() / "main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "blink.ino") << "void setup(){}\n";

    mm::build::LoadPolicy policy{.tool = "build", .external = ext_tree.root()};
    const auto project = mm::build::load_project(proj_tree.root(), policy);
    expect(project.ok, "external blink beside project blink loads cleanly");

    // 2. Duplicate app name in the SAME external root is refused
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: dir\nname: sketches\nproject: " + rel_proj +
        "\nfolder: b1\nfolder: b2\n");
    ext_tree.manifest_raw("b1",
        "mm: 1.3\nkind: app\nname: dup\nfile: main.cpp\nsketch: dup.ino\n");
    ext_tree.manifest_raw("b2",
        "mm: 1.3\nkind: app\nname: dup\nfile: main.cpp\nsketch: dup.ino\n");
    std::filesystem::create_directories(ext_tree.root() / "b1");
    std::filesystem::create_directories(ext_tree.root() / "b2");
    std::ofstream(ext_tree.root() / "b1/main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "b1/dup.ino") << "void setup(){}\n";
    std::ofstream(ext_tree.root() / "b2/main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "b2/dup.ino") << "void setup(){}\n";

    const auto proj_dup = mm::build::load_project(proj_tree.root(), policy);
    expect(!proj_dup.ok,
           "duplicate app name within one external root is refused");
}

void manifest_gate_refusals() {
    const mm::test::scoped_tree proj_tree{"graft_gate_proj"};
    proj_tree.manifest("", "kind: project\nname: proj\n");

    const mm::test::scoped_tree ext_tree{"graft_gate_ext"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();

    // 1. project: on kind that may not carry it
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: module\nname: m\nmodule: mm.m\nproject: " + rel_proj +
        "\nfile: m.cppm\n");
    std::ofstream(ext_tree.root() / "m.cppm") << "export module mm.m;\n";
    mm::build::LoadPolicy policy{.tool = "build", .external = ext_tree.root()};
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "project: on module manifest is refused");

    // 2. Absolute project: path is refused
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + proj_tree.root().string() +
        "\nfile: main.cpp\nsketch: a.ino\n");
    std::ofstream(ext_tree.root() / "main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "a.ino") << "void setup(){}\n";
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "absolute project: value is refused");

    // 3. project: resolving to no directory or not a project
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: ../missing_dir_12345\n"
        "file: main.cpp\nsketch: a.ino\n");
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "project: resolving to missing directory is refused");

    // 4. folder: on external app is refused
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
        "\nfolder: child\nfile: main.cpp\nsketch: a.ino\n");
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "folder: on external app is refused");
}

void source_guard_cases() {
    const mm::test::scoped_tree proj_tree{"graft_source_proj"};
    proj_tree.manifest("", "kind: project\nname: proj\n");

    const mm::test::scoped_tree ext_tree{"graft_source_ext"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();

    mm::build::LoadPolicy policy{.tool = "build", .external = ext_tree.root()};

    // 1. sketch: value that climbs is refused at load
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
        "\nfile: main.cpp\nsketch: ../escaped.ino\n");
    std::ofstream(ext_tree.root() / "main.cpp") << "int main(){}\n";
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "climbing sketch: path is refused");

    // 2. sketch: value that is absolute is refused at load
    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
        "\nfile: main.cpp\nsketch: /tmp/abs.ino\n");
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "absolute sketch: path is refused");

    // 3. sketch: that is a symlink out of tree is refused canonically
    const mm::test::scoped_tree outside_tree{"graft_source_outside"};
    const auto outside_ino = outside_tree.root() / "leak.ino";
    std::ofstream(outside_ino) << "void setup(){}\n";
    const auto sym_ino = ext_tree.root() / "linked.ino";
    std::error_code ec;
    std::filesystem::create_symlink(outside_ino, sym_ino, ec);
    if (!ec) {
        ext_tree.manifest_raw("",
            "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
            "\nfile: main.cpp\nsketch: linked.ino\n");
        expect(!mm::build::load_project(proj_tree.root(), policy).ok,
               "sketch: symlink pointing outside tree is refused");
    }

    // 4. file: in a graft that is a symlink out of tree is refused canonically
    const auto outside_cpp = outside_tree.root() / "leak.cpp";
    std::ofstream(outside_cpp) << "int main(){}\n";
    const auto sym_cpp = ext_tree.root() / "linked.cpp";
    std::filesystem::create_symlink(outside_cpp, sym_cpp, ec);
    if (!ec) {
        std::ofstream(ext_tree.root() / "valid.ino") << "void setup(){}\n";
        ext_tree.manifest_raw("",
            "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
            "\nfile: linked.cpp\nsketch: valid.ino\n");
        expect(!mm::build::load_project(proj_tree.root(), policy).ok,
               "graft file: symlink pointing outside tree is refused");
    }
}

void sketch_library_wiring() {
    const mm::test::scoped_tree proj_tree{"graft_proj_lib"};
    proj_tree.manifest("", "kind: project\nname: proj\n");

    // The library root is the external tree; its example is an app beneath it.
    const mm::test::scoped_tree lib_tree{"graft_ext_lib"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), lib_tree.root()).lexically_normal().string();
    lib_tree.manifest_raw("",
        "mm: 1.3\nkind: dir\nname: AnalogPin\nproject: " + rel_proj +
        "\nfolder: examples\n");
    std::ofstream(lib_tree.root() / "AnalogPin.h") << "#pragma once\n";
    std::ofstream(lib_tree.root() / "AnalogPin.cpp")
        << "int lib(){return 0;}\n";
    std::ofstream(lib_tree.root() / "README.md") << "not a source\n";
    lib_tree.manifest_raw("examples",
        "mm: 1.3\nkind: dir\nname: examples\nfolder: AnalogPin\n");
    lib_tree.manifest_raw("examples/AnalogPin",
        "mm: 1.3\nkind: app\nname: AnalogPin\nfile: main.cpp\n"
        "sketch: AnalogPin.ino\nsketch-library: ../..\n");
    std::ofstream(lib_tree.root() / "examples/AnalogPin/main.cpp")
        << "int main(){}\n";
    std::ofstream(lib_tree.root() / "examples/AnalogPin/AnalogPin.ino")
        << "void setup(){}\nvoid loop(){}\n";

    mm::build::LoadPolicy policy{.tool = "build", .external = lib_tree.root()};
    const auto project = mm::build::load_project(proj_tree.root(), policy);
    expect(project.ok, "library tree with an example app loads");
    expect(project.targets.size() == 1, "one application target");
    const auto& app = project.targets.front();
    expect(app.sketch_libraries.size() == 1, "one sketch library recorded");
    expect(app.sketch_libraries.front() == lib_tree.root(),
           "sketch library resolves to the library root");

    bool has_library_source = false;
    bool has_readme = false;
    for (const auto& unit : app.sources) {
        if (unit.source == lib_tree.root() / "AnalogPin.cpp")
            has_library_source = true;
        if (unit.path.find("README") != std::string::npos) has_readme = true;
    }
    expect(has_library_source, "library source compiles into the application");
    expect(!has_readme, "non-source files stay out of the application");

    std::vector<std::filesystem::path> includes;
    expect(mm::build::library_include_directories(
               proj_tree.root(), project.libraries, app, includes, "build"),
           "include directories resolve");
    expect(includes.size() == 2, "the application and the library are named");
    expect(includes.front() == lib_tree.root() / "examples/AnalogPin",
           "the application directory comes first, for the generated header");
    expect(includes.back() == lib_tree.root(),
           "a flat library puts its root on the include path");

    // A src/ directory takes over both roles.
    std::filesystem::create_directories(lib_tree.root() / "src");
    std::ofstream(lib_tree.root() / "src/inner.cpp")
        << "int inner(){return 0;}\n";
    const auto layered = mm::build::load_project(proj_tree.root(), policy);
    expect(layered.ok, "layered library tree loads");
    includes.clear();
    expect(mm::build::library_include_directories(
               proj_tree.root(), layered.libraries, layered.targets.front(),
               includes, "build"),
           "layered include directories resolve");
    expect(includes.size() == 2 && includes.back() == lib_tree.root() / "src",
           "a layered library puts src/ on the include path");
    bool has_inner = false;
    bool has_root_source = false;
    for (const auto& unit : layered.targets.front().sources) {
        if (unit.source == lib_tree.root() / "src/inner.cpp") has_inner = true;
        if (unit.source == lib_tree.root() / "AnalogPin.cpp")
            has_root_source = true;
    }
    expect(has_inner, "layered library compiles sources under src/");
    expect(!has_root_source, "layered library leaves root sources alone");
}

void sketch_library_refusals() {
    const mm::test::scoped_tree proj_tree{"graft_proj_librefuse"};
    proj_tree.manifest("", "kind: project\nname: proj\n");

    const mm::test::scoped_tree ext_tree{"graft_ext_librefuse"};
    const auto rel_proj = std::filesystem::relative(
        proj_tree.root(), ext_tree.root()).lexically_normal().string();
    std::ofstream(ext_tree.root() / "main.cpp") << "int main(){}\n";
    std::ofstream(ext_tree.root() / "a.ino") << "void setup(){}\n";
    mm::build::LoadPolicy policy{.tool = "build", .external = ext_tree.root()};

    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
        "\nfile: main.cpp\nsketch-library: .\n");
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "sketch-library without sketch: is refused");

    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
        "\nfile: main.cpp\nsketch: a.ino\nsketch-library: ..\n");
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "sketch-library climbing out of the tree is refused");

    ext_tree.manifest_raw("",
        "mm: 1.3\nkind: app\nname: a\nproject: " + rel_proj +
        "\nfile: main.cpp\nsketch: a.ino\nsketch-library: main.cpp\n");
    expect(!mm::build::load_project(proj_tree.root(), policy).ok,
           "sketch-library naming a file is refused");
}

const mm::test::case_ cases[] = {
    {"app root grafting", &app_root_grafting},
    {"dir root grafting", &dir_root_grafting},
    {"graft allowlist refusals", &graft_allowlist_refusals},
    {"uniqueness rules", &uniqueness_rules},
    {"manifest gate refusals", &manifest_gate_refusals},
    {"source guard cases", &source_guard_cases},
    {"sketch library wiring", &sketch_library_wiring},
    {"sketch library refusals", &sketch_library_refusals},
};

const mm::test::registrar reg{"mm.build graft", cases};

}  // namespace
