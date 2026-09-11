#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

void library_definition() {
    const mm::test::scoped_tree tree{"library_definition"};
    tree.manifest("", "kind: project\nname: p\nfolder: libraries\n");
    tree.manifest_raw(
        "libraries",
        "mm: 1.3\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
        "include-directory: include\ninclude-directory: second\nlibrary-directory: lib\n"
        "link-archive: lib/libdemo.a\nlink-input: m\nfolder: wrapper\n");
    tree.manifest_raw(
        "libraries/wrapper",
        "mm: 1.3\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
        "file: wrapper.cppm\nlibrary: demo\n");
    std::ofstream(tree.root() / "libraries/LICENSE") << "fixture licence\n";

    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok, "an absent checkout is a valid unselected library definition");
    expect(project.libraries.size() == 1, "library definition is collected");
    expect(project.targets.size() == 1 && project.targets.front().name == "wrapper",
           "library recurses to its wrapper without becoming a build target");
    expect(!project.libraries.front().checkout_present,
           "an absent source directory is reported as an absent checkout");
    expect(project.targets.front().library == "demo",
           "a module resolves its library declaration");
    expect(project.libraries.front().source == "libraries/third_party" &&
               project.libraries.front().include_directories.front().path ==
                   "libraries/third_party/include",
           "library paths are exposed root relative");
    expect(!mm::build::validate_library_checkout(tree.root(), project.libraries.front()),
           "a selected absent checkout is rejected");
    std::vector<std::filesystem::path> includes;
    expect(!mm::build::library_include_directories(
               tree.root(), project.libraries, project.targets.front(), includes),
           "a reached wrapper rejects an absent checkout");

    std::filesystem::create_directories(tree.root() / "libraries/third_party/include");
    std::filesystem::create_directories(tree.root() / "libraries/third_party/second");
    std::ofstream(tree.root() / "libraries/third_party/include/demo.h") << "#pragma once\n";
    project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.libraries.front().checkout_present,
           "a non-empty source directory is a present checkout");
    expect(mm::build::validate_library_checkout(tree.root(), project.libraries.front()),
           "a selected contained checkout is valid");
    expect(mm::build::library_include_directories(
               tree.root(), project.libraries, project.targets.front(), includes) &&
               includes.size() == 2 &&
               includes.front() == tree.root() / "libraries/third_party/include" &&
               includes.back() == tree.root() / "libraries/third_party/second",
           "a reached wrapper receives its library include interface in declaration order");
    auto unrelated = project.targets.front();
    unrelated.library.clear();
    expect(mm::build::library_include_directories(
               tree.root(), project.libraries, unrelated, includes) && includes.empty(),
           "a module without a library receives no include directories");
    std::error_code ec;
    std::filesystem::remove(tree.root() / "libraries/third_party/second", ec);
    expect(!mm::build::library_include_directories(
               tree.root(), project.libraries, project.targets.front(), includes),
           "a reached wrapper requires each declared include directory");
    std::filesystem::create_directories(tree.root() / "libraries/third_party/second");

    std::filesystem::create_directories(tree.root() / "libraries/outside");
    std::filesystem::create_directory_symlink(tree.root() / "libraries/outside",
                                               tree.root() / "libraries/third_party/escape", ec);
    expect(!ec, "interface escape symlink created");
    tree.manifest_raw(
        "libraries",
        "mm: 1.3\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
        "include-directory: escape\nfolder: wrapper\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "an existing interface symlink cannot escape canonical source");

    std::filesystem::remove(tree.root() / "libraries/third_party/escape", ec);
    std::filesystem::remove(tree.root() / "libraries/LICENSE", ec);
    std::ofstream(tree.root() / "libraries/third_party/LICENSE") << "misplaced licence\n";
    std::filesystem::create_symlink(tree.root() / "libraries/third_party/LICENSE",
                                    tree.root() / "libraries/LICENSE", ec);
    expect(!ec, "licence symlink created");
    tree.manifest_raw(
        "libraries",
        "mm: 1.3\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
        "include-directory: include\nfolder: wrapper\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "a licence cannot resolve inside canonical source");
}

void module_reference_validation() {
    const mm::test::scoped_tree tree{"library_module_reference"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: wrapper\n");
    tree.manifest_raw("library",
                      "mm: 1.3\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\ninclude-directory: include\n");
    tree.manifest_raw("wrapper",
                      "mm: 1.3\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";

    auto project = mm::build::load_project(
        tree.root(), {.tool = "build", .strict_tree = true});
    expect(project.ok && project.targets.front().library == "demo",
           "a module may name one known library without requiring its checkout");

    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module library reference requires manifest version 1.3");

    tree.manifest_raw("wrapper",
                      "mm: 1.3\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: missing\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module cannot reference an unknown library");

    tree.manifest_raw("wrapper",
                      "mm: 1.3\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module cannot name more than one library");
}

void sdk_reference_and_validation() {
    const mm::test::scoped_tree tree{"library_sdk_reference"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: sdk\n");
    tree.manifest_raw("library",
                      "mm: 1.3\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\nlink-input: demo+abi-1.0\n");
    tree.manifest_raw("sdk",
                      "mm: 1.3\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: demo\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";
    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.sdks.front().library == "demo",
           "an SDK resolves a declared library by name");

    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "an SDK library reference requires manifest version 1.3");

    tree.manifest_raw("sdk",
                      "mm: 1.3\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: missing\n");
    project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(!project.ok, "an SDK cannot reference an unknown library");

    tree.manifest_raw("library",
                      "mm: 1.3\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\nlink-input: -Wl,unsafe\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "link-input rejects argument syntax");
}

const mm::test::case_ cases[] = {
    {"library definition", &library_definition},
    {"module library reference", &module_reference_validation},
    {"SDK library reference", &sdk_reference_and_validation},
};

const mm::test::registrar reg{"mm.build library", cases};

}
