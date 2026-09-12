// Black box tests for mm.model::Loaded against a project that is not
// modules.cpp itself. Loaded::load() accepts any tree whose root manifest
// is kind:project (this is what makes it a general adapter rather than one
// hard-coded to this repository), but build0/build1/c++ in tools() and all
// of operations() are fixed data describing this repository's own
// bootstrap.sh specifically. A prior version added those unconditionally,
// so tools() reported a build0/build1/c++ that had nothing to do with a
// foreign project, and operations() embedded a null Tool* into invokes()
// for every one of the six named tools (build, main, mdy, test, check,
// model) a foreign project does not happen to declare. These cases pin the
// fixed correction: load() still succeeds generally, but the
// repository-specific fixed data is absent rather than null or fabricated.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

import mm.model;
import mm.test;
import models.manifest;
import models.repository;
import models.tool;
import models.workflow;

namespace {

// Writes the same foreign (not modules.cpp) project into tree: one
// kind:project with one kind:app named "widget" - deliberately none of the
// six names (build, main, mdy, test, check, model) operations() and the
// fixed part of tools() look for.
void write_foreign_project(const mm::test::scoped_tree& tree) {
    tree.manifest("", "kind: project\nname: unrelated-project\nfolder: a\n");
    tree.manifest("a", "kind: app\nname: widget\nfile: a.cpp\n");
}

void load_of_a_foreign_project_still_succeeds() {
    const mm::test::scoped_tree tree{"foreign_load"};
    write_foreign_project(tree);
    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    mm::test::expect(ok, "expected load() to accept any valid project, not only modules.cpp");
    mm::test::expect(loaded.repository().apps().size() == 1, "expected the one declared app");
}

void a_foreign_project_has_no_bootstrap_tools() {
    const mm::test::scoped_tree tree{"foreign_tools"};
    write_foreign_project(tree);
    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    if (!ok) return;

    const auto tools = loaded.tools();
    mm::test::expect(tools.size() == 1,
                     "expected only the declared \"widget\" tool, no build0/build1/c++");

    for (const auto* tool : tools) {
        mm::test::expect(tool->name() != "build0", "expected no build0 for a foreign project");
        mm::test::expect(tool->name() != "build1", "expected no build1 for a foreign project");
        mm::test::expect(tool->name() != "c++", "expected no c++ for a foreign project");
    }
}

void a_foreign_project_has_no_operations() {
    const mm::test::scoped_tree tree{"foreign_operations"};
    write_foreign_project(tree);
    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    if (!ok) return;

    mm::test::expect(loaded.operations().empty(),
                     "expected no Operations for a project that is not modules.cpp, "
                     "rather than Operations holding a null Tool*");
}

void a_foreign_project_exposes_a_library() {
    const mm::test::scoped_tree tree{"foreign_library"};
    tree.manifest("", "kind: project\nname: unrelated-project\nfolder: library\n"
                      "folder: wrapper\nfolder: sdk\n");
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\ninclude-directory: include\nlink-input: m\n");
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: demo\n");
    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: lib.demo\n"
                      "file: wrapper.cppm\nlibrary: demo\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";
    std::filesystem::create_directories(tree.root() / "library/third_party/include");
    std::ofstream(tree.root() / "library/third_party/.checkout") << "present\n";

    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    mm::test::expect(ok, "expected a foreign project with a library to load");
    if (!ok) return;
    const auto libraries = loaded.repository().libraries();
    mm::test::expect(libraries.size() == 1,
                     "expected the library definition in the repository model");
    if (libraries.empty()) return;
    const auto* library = libraries.front();
    mm::test::expect(library->kind() == models::Kind::Library &&
                         library->source() == "library/third_party" &&
                         library->licence() == "library/LICENSE",
                     "expected root-relative library paths");
    mm::test::expect(library->checkout_present(),
                     "expected the model to observe the non-empty checkout");
    mm::test::expect(library->include_directories().front() ==
                         "library/third_party/include" &&
                         library->link_inputs().front() == "m",
                     "expected ordered public interface declarations");
    const auto sdks = loaded.repository().sdks();
    mm::test::expect(sdks.size() == 1 && sdks.front()->library() == "demo",
                     "expected the SDK model to expose its library reference name");
    const auto modules = loaded.repository().modules();
    mm::test::expect(modules.size() == 1 && modules.front()->library() == "demo",
                     "expected the module model to expose its library reference name");
    mm::test::expect(library->external_build().empty(),
                     "expected empty external_build for ordinary library");
}

void a_foreign_project_exposes_an_external_build_library() {
    const mm::test::scoped_tree tree{"foreign_external_build_library"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\n");
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";
    std::filesystem::create_directories(tree.root() / "library/cmake");
    std::ofstream(tree.root() / "library/cmake/CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n";

    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    mm::test::expect(ok, "expected an external-build library to load in model");
    if (!ok) return;
    const auto libraries = loaded.repository().libraries();
    mm::test::expect(libraries.size() == 1,
                     "expected one library in repository");
    if (libraries.empty()) return;
    mm::test::expect(libraries.front()->external_build() == "cmake",
                     "expected external_build to return cmake");
}

const mm::test::case_ cases[] = {
    { "load of a foreign project still succeeds",       &load_of_a_foreign_project_still_succeeds },
    { "a foreign project has no bootstrap tools",       &a_foreign_project_has_no_bootstrap_tools },
    { "a foreign project has no operations",            &a_foreign_project_has_no_operations },
    { "a foreign project exposes a library",            &a_foreign_project_exposes_a_library },
    { "a foreign project exposes an external build library", &a_foreign_project_exposes_an_external_build_library },
};

const mm::test::registrar reg{"mm.model foreign project", cases};

}  // namespace
