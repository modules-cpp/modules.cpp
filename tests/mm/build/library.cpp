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
        "mm: 1.2\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
        "include-directory: include\ninclude-directory: second\nlibrary-directory: lib\n"
        "link-archive: lib/libdemo.a\nlink-input: m\nfolder: wrapper\n");
    tree.manifest_raw(
        "libraries/wrapper",
        "mm: 1.2\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
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
        "mm: 1.2\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
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
        "mm: 1.2\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
        "include-directory: include\nfolder: wrapper\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "a licence cannot resolve inside canonical source");
}

// A library's folder: entries reach the wrappers beside its vendored tree. The
// tree itself must stay out of the walk however a folder: entry spells it,
// because a manifest found inside it would otherwise become an ordinary target
// built from foreign source and handed to check.
void source_boundary() {
    const mm::test::scoped_tree tree{"library_source_boundary"};
    tree.manifest("", "kind: project\nname: p\nfolder: libraries\n");

    const std::string library =
        "mm: 1.2\nkind: library\nname: demo\nsource: third_party\nlicence: LICENSE\n"
        "include-directory: include\n";

    tree.manifest_raw("libraries", library + "folder: wrapper\n");
    std::ofstream(tree.root() / "libraries/LICENSE") << "fixture licence\n";
    tree.manifest_raw("libraries/wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\n");

    // Foreign manifests inside the checkout: exactly what must never be reached.
    std::filesystem::create_directories(tree.root() / "libraries/third_party/include");
    tree.manifest_raw("libraries/third_party",
                      "mm: 1.0\nkind: module\nname: foreign\nmodule: foreign\n"
                      "file: foreign.cppm\n");
    tree.manifest_raw("libraries/third_party/inner",
                      "mm: 1.0\nkind: module\nname: inner\nmodule: foreign.inner\n"
                      "file: inner.cppm\n");

    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.targets.size() == 1 &&
               project.targets.front().name == "wrapper",
           "a wrapper beside the vendored tree is reached and the tree is not");

    tree.manifest_raw("libraries", library + "folder: wrapper\nfolder: third_party\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "a library folder entry cannot name its own source");

    std::error_code ec;
    std::filesystem::create_directory_symlink(tree.root() / "libraries/third_party/inner",
                                              tree.root() / "libraries/alias", ec);
    expect(!ec, "folder alias symlink created");
    tree.manifest_raw("libraries", library + "folder: wrapper\nfolder: alias\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "a library folder entry cannot reach source through a symlink");

    // The alias is rejected for where it resolves, not for being a symlink.
    std::filesystem::create_directories(tree.root() / "libraries/beside");
    tree.manifest_raw("libraries/beside",
                      "mm: 1.2\nkind: module\nname: beside\nmodule: p.beside\n"
                      "file: beside.cppm\n");
    std::filesystem::remove(tree.root() / "libraries/alias", ec);
    std::filesystem::create_directory_symlink(tree.root() / "libraries/beside",
                                              tree.root() / "libraries/alias", ec);
    expect(!ec, "sibling alias symlink created");
    project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.targets.size() == 2,
           "a folder symlink resolving outside source is followed normally");
}

void module_reference_validation() {
    const mm::test::scoped_tree tree{"library_module_reference"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: wrapper\n");
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\ninclude-directory: include\n");
    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";

    auto project = mm::build::load_project(
        tree.root(), {.tool = "build", .strict_tree = true});
    expect(project.ok && project.targets.front().library == "demo",
           "a module may name one known library without requiring its checkout");

    tree.manifest_raw("wrapper",
                      "mm: 1.1\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module library reference requires manifest version 1.2");

    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: missing\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module cannot reference an unknown library");

    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: p.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module cannot name more than one library");
}

void sdk_reference_and_validation() {
    const mm::test::scoped_tree tree{"library_sdk_reference"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: sdk\n");
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\nlink-input: demo+abi-1.0\n");
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: demo\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";
    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.sdks.front().library == "demo",
           "an SDK resolves a declared library by name");

    tree.manifest_raw("sdk",
                      "mm: 1.1\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "an SDK library reference requires manifest version 1.2");

    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: target-sdk\ntarget: m68k-linux-gnu\n"
                      "compiler-family: gcc\nruntime: glibc\nlibrary: missing\n");
    project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(!project.ok, "an SDK cannot reference an unknown library");

    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\nlink-input: -Wl,unsafe\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "link-input rejects argument syntax");
}

void external_build_validation() {
    const mm::test::scoped_tree tree{"external_build_val"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\n");
    std::filesystem::create_directories(tree.root() / "library");
    std::ofstream(tree.root() / "library/LICENSE") << "licence\n";

    // 1. external-build requires mm: 1.2
    tree.manifest_raw("library",
                      "mm: 1.1\nkind: library\nname: extlib\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "external-build requires manifest version 1.2");

    // 2. rejects unknown external-build system
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: extlib\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: ninja\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "external-build rejects unknown build system");

    // 3. rejects external-build without cmake directory
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: extlib\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "external-build requires cmake directory beside manifest");

    // 4. rejects external-build without CMakeLists.txt in cmake directory
    std::filesystem::create_directories(tree.root() / "library/cmake");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "external-build requires cmake/CMakeLists.txt");

    // 5. rejects cmake directory inside source
    std::ofstream(tree.root() / "library/cmake/CMakeLists.txt") << "# bridge\n";
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: extlib\nsource: .\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "cmake directory must resolve outside library source");

    // 6. accepts valid external-build library
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: extlib\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.libraries.front().external_build == "cmake",
           "valid external-build library loads with external_build == cmake");
}

void external_build_sdk_and_board_rules() {
    const mm::test::scoped_tree tree{"external_sdk_board"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: sdk\nfolder: board\n");
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: extlib\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    std::ofstream(tree.root() / "library/LICENSE") << "licence\n";
    std::filesystem::create_directories(tree.root() / "library/cmake");
    std::ofstream(tree.root() / "library/cmake/CMakeLists.txt") << "# bridge\n";

    // SDK referencing external-build library rejecting specs-profile
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: my-sdk\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: none\nlibrary: extlib\n"
                      "specs-profile: rdimon\n");
    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: my-board\nsdk: my-sdk\n"
                      "cpu: cortex-m0plus\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "SDK referencing external-build library rejects specs-profile");

    // SDK referencing external-build library rejecting specs-file
    std::ofstream(tree.root() / "sdk/test.specs") << "specs\n";
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: my-sdk\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: none\nlibrary: extlib\n"
                      "specs-file: test.specs\n"
                      "provides: runtime-init\nprovides: syscalls\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "SDK referencing external-build library rejects specs-file");

    // Valid external SDK and board without linker-script, file, or provides
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: my-sdk\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: none\nlibrary: extlib\n"
                      "provides: reset-vector\nprovides: initial-stack\n"
                      "provides: memory-layout\nprovides: runtime-init\nprovides: syscalls\n");
    auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok && project.boards.size() == 1 &&
               project.boards.front().linker_script.empty() &&
               project.boards.front().sources.empty() &&
               project.boards.front().provides.empty(),
           "board beneath external-build SDK permits omitting linker-script, file, and provides");

    // Ordinary SDK (no external-build library) requires board to have linker-script and file
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: ordinary-sdk\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: newlib\n"
                      "specs-profile: rdimon\n");
    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: ord-board\nsdk: ordinary-sdk\n"
                      "cpu: cortex-m0plus\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "board beneath ordinary SDK requires linker-script");

    std::ofstream(tree.root() / "board/link.ld") << "SECTIONS {}\n";
    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: ord-board\nsdk: ordinary-sdk\n"
                      "cpu: cortex-m0plus\ninstruction-set: thumb\nfloat-abi: soft\n"
                      "linker-script: link.ld\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "board beneath ordinary SDK requires at least one file");
}

void external_wrapper_availability() {
    const mm::test::scoped_tree tree{"external_wrapper_avail"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: sdk\nfolder: wrapper\nfolder: other_sdk\n");
    std::filesystem::create_directories(tree.root() / "library");
    std::ofstream(tree.root() / "library/LICENSE") << "licence\n";
    std::filesystem::create_directories(tree.root() / "library/cmake");
    std::ofstream(tree.root() / "library/cmake/CMakeLists.txt") << "# bridge\n";
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: extlib\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: ext-wrapper\nmodule: ext.wrapper\n"
                      "file: wrapper.cppm\nlibrary: extlib\n");
    std::ofstream(tree.root() / "wrapper/wrapper.cppm") << "export module ext.wrapper;\n";

    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: match-sdk\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: none\nlibrary: extlib\n"
                      "provides: reset-vector\nprovides: initial-stack\n"
                      "provides: memory-layout\nprovides: runtime-init\nprovides: syscalls\n");
    tree.manifest_raw("other_sdk",
                      "mm: 1.2\nkind: sdk\nname: other-sdk\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: newlib\nspecs-profile: rdimon\n");

    const auto project = mm::build::load_project(
        tree.root(), {.tool = "configure", .strict_tree = true});
    expect(project.ok, "project loads successfully");

    std::size_t wrapper_node = mm::build::no_target;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (project.nodes[i].name == "ext-wrapper") wrapper_node = i;
    }
    expect(wrapper_node != mm::build::no_target, "wrapper node found");

    // 1. In host lane: unavailable, reason names the library
    auto avail_host = mm::build::availability(project, wrapper_node, true, false, nullptr);
    expect(!avail_host.available, "external wrapper is unavailable in host lane");
    expect(avail_host.reason.find("extlib") != std::string::npos,
           "host lane availability reason names the library");

    // 2. In target lane without SDK / platform: unavailable
    auto avail_target_no_platform = mm::build::availability(project, wrapper_node, true, true, nullptr);
    expect(!avail_target_no_platform.available, "external wrapper unavailable with null platform");
    expect(avail_target_no_platform.reason.find("extlib") != std::string::npos,
           "null platform availability reason names the library");

    // 3. In target lane with different SDK: unavailable
    mm::build::Platform other_platform;
    other_platform.sdk = "other-sdk";
    auto avail_other = mm::build::availability(project, wrapper_node, true, true, &other_platform);
    expect(!avail_other.available, "external wrapper unavailable with SDK that does not name the library");
    expect(avail_other.reason.find("extlib") != std::string::npos,
           "different SDK availability reason names the library");

    // 4. In target lane with matching SDK: available
    mm::build::Platform match_platform;
    match_platform.sdk = "match-sdk";
    auto avail_match = mm::build::availability(project, wrapper_node, true, true, &match_platform);
    expect(avail_match.available, "external wrapper available in target lane with matching SDK");
}

const mm::test::case_ cases[] = {
    {"library definition", &library_definition},
    {"library source boundary", &source_boundary},
    {"module library reference", &module_reference_validation},
    {"SDK library reference", &sdk_reference_and_validation},
    {"external-build validation", &external_build_validation},
    {"external-build SDK and board rules", &external_build_sdk_and_board_rules},
    {"external-wrapper availability", &external_wrapper_availability},
};

const mm::test::registrar reg{"mm.build library", cases};

}
