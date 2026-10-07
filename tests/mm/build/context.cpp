// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <fstream>
#include <string>
import mm.build;
import mm.test;

namespace {
using mm::test::expect;
void connected_roots() {
    const mm::test::scoped_tree tree{"external_connected"};
    tree.manifest_raw("", "mm: 1.3\nkind: dir\nname: root\nfolder: apps\n");
    tree.manifest_raw("apps", "mm: 1.3\nkind: dir\nname: apps\nfolder: first\n");
    tree.manifest_raw("apps/first", "mm: 1.3\nkind: app\nname: first\nfile: main.cpp\n");
    tree.manifest_raw("apps/other", "mm: 1.3\nkind: app\nname: other\nfile: main.cpp\n");
    expect(mm::build::connected_external_root(tree.root() / "apps/first") == tree.root(), "connected directories share outer root");
    expect(mm::build::connected_external_root(tree.root() / "apps/other") == tree.root() / "apps/other", "unregistered ancestor is not a root");
}
void module_declarations() {
    const mm::test::scoped_tree tree{"external_source"};
    const auto source = tree.root() / "source.cpp";
    for (const auto text : {"export module mm.custom;", "module mm.custom;", "module mm.custom:part;", "int value=1'000; export module mm.custom;"}) {
        std::ofstream(source) << text;
        expect(!mm::build::external_source_is_ordinary(source), "actual named module is refused");
    }
    for (const auto text : {"// export module mm.custom;\nint main(){}", "/* module mm.custom; */ int main(){}",
            "const char* text=\"export module fake;\";", "const char* text=R\"tag(module fake;)tag\"; int main(){}",
            "const char* text=u8R\"tag(\"module fake;)tag\"; int main(){}",
            "import mm.stdio; int main(){}", "int module = 1; int main(){return module;}", "module; import mm.stdio;"}) {
        std::ofstream(source) << text;
        expect(mm::build::external_source_is_ordinary(source), "comments, strings, imports and ordinary identifiers are accepted");
    }
}
void output_ownership() {
    const mm::test::scoped_tree tree{"external_owned"};
    mm::build::ResolvedRoots roots;
    roots.external_root = tree.root();
    roots.managed_external = true;
    expect(mm::build::register_external_outputs(roots, {"out-host"}), "register owned lane");
    const auto lane = tree.root() / "out-host";
    expect(mm::build::prepare_external_build(roots, lane, "first"), "initial lane preparation");
    std::ofstream(lane / "sentinel") << "previous";
    expect(mm::build::prepare_external_build(roots, lane, "first"), "same identity keeps cache");
    expect(std::filesystem::exists(lane / "sentinel"), "unchanged identity retains outputs");
    expect(mm::build::prepare_external_build(roots, lane, "changed"), "new identity resets owned cache");
    expect(!std::filesystem::exists(lane / "sentinel"), "old cache removed after rebinding");
    std::filesystem::create_directories(tree.root() / "out-other");
    std::ofstream(tree.root() / "out-other/sentinel") << "user";
    expect(!mm::build::prepare_external_build(roots, tree.root() / "out-other", "new"), "unowned outputs refused");
    expect(std::filesystem::exists(tree.root() / "out-other/sentinel"), "user data survives refusal");
    std::filesystem::create_directory_symlink(tree.root() / "out-other", tree.root() / "out-target-escape");
    expect(!mm::build::register_external_outputs(roots, {"out-target-escape"}), "symlink output refused");
    expect(!mm::build::register_external_outputs(roots, {"../out-host"}), "escaping output refused");
}
void ordinary_graft() {
    const mm::test::scoped_tree installation{"external_installation"};
    installation.manifest_raw("", "mm: 1.3\nkind: project\nname: installation\n");
    const mm::test::scoped_tree app{"external_ordinary"};
    app.manifest_raw("", "mm: 1.3\nkind: app\nname: app\nfile: main.cpp\nfile: helper.cpp\n");
    std::ofstream(app.root() / "main.cpp") << "int helper(); int main(){return helper();}\n";
    std::ofstream(app.root() / "helper.cpp") << "int helper(){return 0;}\n";
    const auto project = mm::build::load_project(installation.root(), {
        .tool="configure", .external=app.root(), .managed_external=true});
    expect(project.ok, "locator-free ordinary app loads with managed policy");
    expect(project.targets.size() == 1 && project.targets.front().sources.size() == 2, "multiple ordinary units collected");
    std::ofstream(app.root() / "helper.cpp") << "export module mm.custom;\n";
    expect(!mm::build::load_project(installation.root(), {
        .tool="configure", .external=app.root(), .managed_external=true}).ok, "named external module rejected during manifest loading");
}
const mm::test::case_ cases[] = {
    {"connected external roots", &connected_roots},
    {"external named module declarations", &module_declarations},
    {"owned external outputs", &output_ownership},
    {"ordinary external source graft", &ordinary_graft},
};
const mm::test::registrar reg{"mm.build external context", cases};
}
