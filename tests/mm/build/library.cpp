#include <filesystem>
#include <fstream>
#include <string>

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
        "include-directory: include\nlibrary-directory: lib\n"
        "link-archive: lib/libdemo.a\nlink-input: m\nfolder: wrapper\n");
    tree.manifest("libraries/wrapper",
                  "kind: module\nname: wrapper\nmodule: p.wrapper\nfile: wrapper.cppm\n");
    std::ofstream(tree.root() / "libraries/LICENSE") << "fixture licence\n";

    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok, "an absent checkout is a valid unselected library definition");
    expect(project.libraries.size() == 1, "library definition is collected");
    expect(project.targets.size() == 1 && project.targets.front().name == "wrapper",
           "library recurses to its wrapper without becoming a build target");
    expect(!project.libraries.front().checkout_present,
           "an absent source directory is reported as an absent checkout");
    expect(project.libraries.front().source == "libraries/third_party" &&
               project.libraries.front().include_directories.front().path ==
                   "libraries/third_party/include",
           "library paths are exposed root relative");
    expect(!mm::build::validate_library_checkout(tree.root(), project.libraries.front()),
           "a selected absent checkout is rejected");

    std::filesystem::create_directories(tree.root() / "libraries/third_party/include");
    std::ofstream(tree.root() / "libraries/third_party/include/demo.h") << "#pragma once\n";
    project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.libraries.front().checkout_present,
           "a non-empty source directory is a present checkout");
    expect(mm::build::validate_library_checkout(tree.root(), project.libraries.front()),
           "a selected contained checkout is valid");

    std::error_code ec;
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
    {"SDK library reference", &sdk_reference_and_validation},
};

const mm::test::registrar reg{"mm.build library", cases};

}
