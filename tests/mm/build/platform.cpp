<<<<<<< HEAD
// Manifest-side platform definition and version-gate tests.
#include <fstream>
=======
// Platform-side tests: provider and board selection, availability, library
// checkout validation and include directories.
//
// Selection, availability, and provider binding are exercised through
// loaded manifest trees and, where the API allows, pure functions over a
// Project. Board derivation and name grammar live in the manifest suite.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
>>>>>>> f0dd29c (test: group provider and board tests under mm.build platform)

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

<<<<<<< HEAD
void make_platform_tree(const mm::test::scoped_tree& tree) {
    tree.manifest("", "kind: project\nname: p\nfolder: platforms\n");
    tree.manifest("platforms",
                  "kind: dir\nname: platforms\nfolder: sdk\nfolder: board\n");
=======
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
        "mm: 1.2\nkind: module\nname: wrapper\nmodule: lib.wrapper\n"
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
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: lib.wrapper\n"
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
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: lib.wrapper\n"
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
           "a library wrapper module must use the lib. prefix");

    tree.manifest_raw("wrapper",
                      "mm: 1.1\nkind: module\nname: wrapper\nmodule: lib.wrapper\n"
                      "file: wrapper.cppm\nlibrary: demo\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module library reference requires manifest version 1.2");

    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: lib.wrapper\n"
                      "file: wrapper.cppm\nlibrary: missing\n");
    expect(!mm::build::load_project(
                tree.root(), {.tool = "build", .strict_tree = true}).ok,
           "a module cannot reference an unknown library");

    tree.manifest_raw("wrapper",
                      "mm: 1.2\nkind: module\nname: wrapper\nmodule: lib.wrapper\n"
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
                      "mm: 1.2\nkind: module\nname: ext-wrapper\nmodule: lib.wrapper\n"
                      "file: wrapper.cppm\nlibrary: extlib\n");
    std::ofstream(tree.root() / "wrapper/wrapper.cppm") << "export module lib.wrapper;\n";

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

using mm::test::expect;

constexpr std::string_view sdk_header =
    "mm: 1.2\nkind: sdk\nname: demo-sdk\ntarget: arm-none-eabi\ncompiler-family: gcc\n"
    "runtime: newlib\nspecs-profile: rdimon\n";

constexpr std::string_view board_header =
    "mm: 1.2\nkind: board\nname: demo-board\nsdk: demo-sdk\ncpu: cortex-m0plus\n"
    "instruction-set: thumb\nfloat-abi: soft\nlinker-script: link.ld\nfile: vectors.cpp\n";

// A project whose interface, two providers, SDK, and board are all present.
// Each caller appends the declarations the case is about.
void write_tree(const mm::test::scoped_tree& tree, std::string_view sdk_extra,
                std::string_view board_extra, std::string_view interface_extra = "") {
    tree.manifest("", "kind: project\nname: p\nfolder: iface\nfolder: first\nfolder: second\n"
                      "folder: app\nfolder: sdk\nfolder: board\n");
    tree.manifest_raw("iface", std::string("mm: 1.2\nkind: module\nname: iface\n"
                                           "module: mm.iface\nfile: iface.cppm\n") +
                                   std::string(interface_extra));
    tree.manifest_raw("first", "mm: 1.2\nkind: module\nname: first\nmodule: platform.first.iface\n"
                               "use: mm.iface\nfile: first.cppm\n");
    tree.manifest_raw("second", "mm: 1.2\nkind: module\nname: second\n"
                                "module: platform.second.iface\nuse: mm.iface\n"
                                "file: second.cppm\n");
    tree.manifest_raw("app", "mm: 1.2\nkind: app\nname: demo-app\nuse: mm.iface\nfile: main.cpp\n");
    tree.manifest_raw("sdk", std::string(sdk_header) + std::string(sdk_extra));
    tree.manifest_raw("board", std::string(board_header) + std::string(board_extra));

    // A board's linker-script and file: entries are resolved against the working
    // tree, so they have to exist for the definition to parse at all.
    std::ofstream(tree.root() / "board/link.ld") << "/* fixture */\n";
    std::ofstream(tree.root() / "board/vectors.cpp") << "// fixture\n";
}

mm::build::Project load(const mm::test::scoped_tree& tree) {
    return mm::build::load_project(tree.root(), {.tool = "configure", .strict_tree = true});
}

std::size_t node_named(const mm::build::Project& project, std::string_view name) {
    for (std::size_t i = 0; i < project.nodes.size(); ++i)
        if (project.nodes[i].name == name) return i;
    return mm::build::no_target;
}

// --- the marker ---------------------------------------------------------

void marker_grammar() {
    {
        const mm::test::scoped_tree tree{"provider_marker_ok"};
        write_tree(tree, "", "", "platform-interface:\n");
        const auto project = load(tree);
        expect(project.ok, "a valueless platform-interface marker loads");
        const auto node = node_named(project, "iface");
        expect(node != mm::build::no_target &&
                   project.targets[project.target[node]].platform_interface,
               "the marker is carried on the interface module's target");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_value"};
        write_tree(tree, "", "", "platform-interface: mm.iface\n");
        expect(!load(tree).ok, "platform-interface with a value is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_twice"};
        write_tree(tree, "", "", "platform-interface:\nplatform-interface:\n");
        expect(!load(tree).ok, "a repeated platform-interface marker is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_kind"};
        tree.manifest("", "kind: project\nname: p\nfolder: app\n");
        tree.manifest_raw("app", "mm: 1.2\nkind: app\nname: demo-app\nfile: main.cpp\n"
                                 "platform-interface:\n");
        expect(!load(tree).ok, "platform-interface is rejected on a kind other than module");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_version"};
        tree.manifest("", "kind: project\nname: p\nfolder: iface\n");
        tree.manifest_raw("iface", "mm: 1.1\nkind: module\nname: iface\nmodule: mm.iface\n"
                                   "file: iface.cppm\nplatform-interface:\n");
        expect(!load(tree).ok, "an older manifest version rejects the marker rather than "
                               "assigning it an older meaning");
    }
}

// --- reference resolution -----------------------------------------------

void provider_grammar_and_references() {
    {
        const mm::test::scoped_tree tree{"provider_binding_ok"};
        write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "",
                   "platform-interface:\n");
        const auto project = load(tree);
        expect(project.ok, "a resolved binding loads");
        expect(project.sdks.size() == 1 && project.sdks.front().providers.size() == 1 &&
                   project.sdks.front().providers.front().interface_module == "mm.iface" &&
                   project.sdks.front().providers.front().provider_module ==
                       "platform.first.iface",
               "the SDK carries the declared binding");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_arity"};
        write_tree(tree, "platform-provider: mm.iface\n", "", "platform-interface:\n");
        expect(!load(tree).ok, "a binding naming only an interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_extra"};
        write_tree(tree, "platform-provider: mm.iface platform.first.iface extra\n", "",
                   "platform-interface:\n");
        expect(!load(tree).ok, "a binding with a third field is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unmarked"};
        write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "");
        expect(!load(tree).ok,
               "binding a module that is not marked as a platform interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unknown_iface"};
        write_tree(tree, "platform-provider: mm.absent platform.first.iface\n", "",
                   "platform-interface:\n");
        expect(!load(tree).ok, "binding an unknown interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unknown_provider"};
        write_tree(tree, "platform-provider: mm.iface platform.absent.iface\n", "",
                   "platform-interface:\n");
        expect(!load(tree).ok, "binding an unknown provider module is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_self"};
        write_tree(tree, "platform-provider: mm.iface mm.iface\n", "", "platform-interface:\n");
        expect(!load(tree).ok, "binding an interface to itself is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unrelated"};
        tree.manifest("", "kind: project\nname: p\nfolder: iface\nfolder: other\nfolder: sdk\n");
        tree.manifest_raw("iface", "mm: 1.2\nkind: module\nname: iface\nmodule: mm.iface\n"
                                   "file: iface.cppm\nplatform-interface:\n");
        tree.manifest_raw("other", "mm: 1.2\nkind: module\nname: other\nmodule: mm.other\n"
                                   "file: other.cppm\n");
        tree.manifest_raw("sdk", std::string(sdk_header) +
                                     "platform-provider: mm.iface mm.other\n");
        expect(!load(tree).ok,
               "a provider whose use closure does not reach its interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_duplicate"};
        write_tree(tree,
                   "platform-provider: mm.iface platform.first.iface\n"
                   "platform-provider: mm.iface platform.second.iface\n",
                   "", "platform-interface:\n");
        expect(!load(tree).ok, "two bindings for one interface in one SDK are rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_duplicate_board"};
        write_tree(tree, "",
                   "platform-provider: mm.iface platform.first.iface\n"
                   "platform-provider: mm.iface platform.second.iface\n",
                   "platform-interface:\n");
        expect(!load(tree).ok, "two bindings for one interface on one board are rejected");
    }
}

// --- selection ----------------------------------------------------------

void precedence_and_selection() {
    const mm::test::scoped_tree tree{"provider_precedence"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n",
               "platform-provider: mm.iface platform.second.iface\n", "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "a project binding one interface from both owners loads");

    // An SDK alone binds its default.
    mm::build::Platform sdk_only;
    sdk_only.sdk = "demo-sdk";
    const auto by_sdk = mm::build::platform_providers(project, true, &sdk_only, "test");
    const auto* sdk_binding = by_sdk.binding("mm.iface");
    expect(sdk_binding != nullptr && sdk_binding->provider_module == "platform.first.iface" &&
               sdk_binding->owner == "demo-sdk" && !sdk_binding->from_board,
           "a selected SDK supplies its default provider");

    // The board overrides it, and does so structurally: the board's binding is
    // declared after the SDK's here, but the rule is most specific wins rather
    // than last declaration wins.
    mm::build::Platform with_board;
    with_board.sdk = "demo-sdk";
    with_board.board = "demo-board";
    const auto by_board = mm::build::platform_providers(project, true, &with_board, "test");
    const auto* board_binding = by_board.binding("mm.iface");
    expect(board_binding != nullptr && board_binding->provider_module == "platform.second.iface" &&
               board_binding->owner == "demo-board" && board_binding->from_board,
           "a selected board overrides its SDK's default for the same interface");
    expect(by_board.effective.size() == 1,
           "an overridden interface resolves to exactly one effective provider");

    // Both modules are declared providers in every lane; only one is selected.
    expect(by_board.declares_provider("platform.first.iface") &&
               by_board.declares_provider("platform.second.iface"),
           "every module named by a declaration is a provider target");
    expect(!by_board.selects_provider("platform.first.iface") &&
               by_board.selects_provider("platform.second.iface"),
           "only the effective provider is selected");

    // The host lane selects no platform at all.
    const auto host = mm::build::platform_providers(project, false, nullptr, "test");
    expect(host.binding("mm.iface") == nullptr, "a host lane binds no provider");
    expect(host.interfaces.size() == 1 && host.interfaces.front() == "mm.iface",
           "the interface is still known in a lane that binds nothing");
}

void requirements_follow_the_authored_closure() {
    const mm::test::scoped_tree tree{"provider_requirements"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "",
               "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "the project loads");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");

    const auto app = node_named(project, "demo-app");
    expect(app != mm::build::no_target, "the application node is found");
    expect(providers.requirements[app].size() == 1 &&
               providers.requirements[app].front() == "mm.iface",
           "an application reaching the interface requires it");
    expect(providers.unmet_requirement(app).empty(),
           "a requirement a selected platform binds is met");

    const auto sdk_node = node_named(project, "demo-sdk");
    expect(providers.requirements[sdk_node].empty(),
           "a node that is not a target requires nothing");
}

void requirements_follow_selected_provider_closures() {
    const auto write_nested = [](const mm::test::scoped_tree& tree, bool bind_second) {
        tree.manifest("", "kind: project\nname: p\nfolder: iface-a\nfolder: iface-b\n"
                          "folder: provider-a\nfolder: provider-b\nfolder: app\nfolder: sdk\n");
        tree.manifest_raw("iface-a", "mm: 1.2\nkind: module\nname: iface-a\n"
                                     "module: mm.iface_a\nfile: iface.cppm\n"
                                     "platform-interface:\n");
        tree.manifest_raw("iface-b", "mm: 1.2\nkind: module\nname: iface-b\n"
                                     "module: mm.iface_b\nfile: iface.cppm\n"
                                     "platform-interface:\n");
        tree.manifest_raw("provider-a", "mm: 1.2\nkind: module\nname: provider-a\n"
                                        "module: platform.iface_a\nuse: mm.iface_a\n"
                                        "use: mm.iface_b\nfile: provider.cppm\n");
        tree.manifest_raw("provider-b", "mm: 1.2\nkind: module\nname: provider-b\n"
                                        "module: platform.iface_b\nuse: mm.iface_b\n"
                                        "file: provider.cppm\n");
        tree.manifest_raw("app", "mm: 1.2\nkind: app\nname: nested-app\n"
                                  "use: mm.iface_a\nfile: main.cpp\n");
        tree.manifest_raw(
            "sdk", std::string(sdk_header) +
                       "platform-provider: mm.iface_a platform.iface_a\n" +
                       (bind_second
                            ? "platform-provider: mm.iface_b platform.iface_b\n"
                            : ""));
    };

    {
        const mm::test::scoped_tree tree{"provider_nested_unmet"};
        write_nested(tree, false);
        const auto project = load(tree);
        expect(project.ok, "a provider may use a second platform interface");
        mm::build::Platform platform;
        platform.sdk = "demo-sdk";
        const auto providers = mm::build::platform_providers(project, true, &platform, "test");
        const auto app = node_named(project, "nested-app");
        expect(providers.requirements[app].size() == 2,
               "provider requirements expand transitively");
        expect(providers.unmet_requirement(app).find("mm.iface_b") != std::string::npos,
               "a missing nested provider is diagnosed before compilation");
    }
    {
        const mm::test::scoped_tree tree{"provider_nested_met"};
        write_nested(tree, true);
        const auto project = load(tree);
        expect(project.ok, "both nested bindings load");
        mm::build::Platform platform;
        platform.sdk = "demo-sdk";
        const auto providers = mm::build::platform_providers(project, true, &platform, "test");
        const auto app = node_named(project, "nested-app");
        expect(providers.requirements[app].size() == 2 &&
                   providers.unmet_requirement(app).empty(),
               "a complete nested provider set is available");
    }
}

void an_unmet_requirement_names_the_platform() {
    const mm::test::scoped_tree tree{"provider_unmet"};
    write_tree(tree, "", "", "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "a project with no binding at all still loads");

    const auto app = node_named(project, "demo-app");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    platform.board = "demo-board";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");

    const auto reason = providers.unmet_requirement(app);
    expect(reason.find("mm.iface") != std::string::npos, "the reason names the interface");
    expect(reason.find("demo-board") != std::string::npos, "the reason names the board");
    expect(reason.find("demo-sdk") != std::string::npos, "the reason names the SDK");

    const auto unavailable =
        mm::build::availability(project, app, true, true, &platform, &providers);
    expect(!unavailable.available, "an executable with an unmet requirement is unavailable");
    expect(unavailable.reason.find("demo-app") != std::string::npos,
           "the availability reason names the executable");

    // Compiling and modelling the interface never needs a platform: only an
    // executable that reaches it does.
    const auto iface = node_named(project, "iface");
    expect(mm::build::availability(project, iface, true, true, &platform, &providers).available,
           "the interface module itself stays available with no provider bound");
}

void an_unbuildable_selected_provider_makes_the_executable_unavailable() {
    const mm::test::scoped_tree tree{"provider_unbuildable"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "",
               "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "the project with a selected provider loads");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");
    const auto app = node_named(project, "demo-app");
    const auto provider = node_named(project, "first");
    expect(app != mm::build::no_target && provider != mm::build::no_target,
           "the application and provider nodes are found");
    if (app == mm::build::no_target || provider == mm::build::no_target) return;

    std::vector<bool> capabilities(project.nodes.size(), true);
    capabilities[provider] = false;
    const auto unavailable = mm::build::availability(
        project, app, capabilities[app], true, &platform, &providers, &capabilities);
    expect(!unavailable.available,
           "an executable cannot outlive its selected provider's lane capability");
    expect(unavailable.reason.find("platform.first.iface") != std::string::npos &&
               unavailable.reason.find("not buildable-target") != std::string::npos,
           "the diagnostic names the provider and failed target capability");
}

void a_provider_is_never_an_independent_root() {
    // Both modules are named by a declaration; only the SDK's is selected,
    // because no board is.
    const mm::test::scoped_tree tree{"provider_roots"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n",
               "platform-provider: mm.iface platform.second.iface\n", "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "the project loads");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");

    const auto selected = node_named(project, "first");
    const auto unselected = node_named(project, "second");
    expect(mm::build::availability(project, selected, true, true, &platform, &providers).available,
           "the selected provider is available");
    const auto skipped =
        mm::build::availability(project, unselected, true, true, &platform, &providers);
    expect(!skipped.available, "an unselected provider is an unavailable skipped target");
    expect(skipped.reason.find("platform.second.iface") != std::string::npos,
           "the reason names the provider module");

    // Without the analysis, nothing classifies a provider, and the old answer
    // stands. This is what keeps every other caller of availability unchanged.
    expect(mm::build::availability(project, unselected, true, true, &platform).available,
           "an unanalysed lane treats a provider as an ordinary module");
}

using mm::test::expect;

void make_base_tree(const mm::test::scoped_tree& tree) {
    tree.manifest("", "kind: project\nname: p\nfolder: platforms\nfolder: boards\n");
    tree.manifest("platforms", "kind: dir\nname: platforms\nfolder: sdk\nfolder: board\n");
>>>>>>> f0dd29c (test: group provider and board tests under mm.build platform)
    tree.manifest_raw("platforms/sdk",
                      "mm: 1.2\nkind: sdk\nname: arm-none-eabi-newlib\n"
                      "target: arm-none-eabi\ncompiler-family: gcc\nruntime: newlib\n"
                      "specs-profile: rdimon\n");
    tree.manifest_raw("platforms/board",
                      "mm: 1.2\nkind: board\nname: mps2-an385\n"
                      "sdk: arm-none-eabi-newlib\ncpu: cortex-m3\n"
                      "instruction-set: thumb\nfloat-abi: soft\n"
                      "machine: mps2-an385\nlinker-script: link.ld\n"
                      "file: vectors.cpp\nprovides: reset-vector\n"
                      "provides: initial-stack\nprovides: memory-layout\n");
    std::ofstream(tree.root() / "platforms/board/link.ld") << "SECTIONS {}\n";
    std::ofstream(tree.root() / "platforms/board/vectors.cpp") << "int vector;\n";
<<<<<<< HEAD
}

void loads_sdk_and_board_definitions() {
    const mm::test::scoped_tree tree{"platform_definitions"};
    make_platform_tree(tree);
    const auto project = mm::build::load_project(tree.root());
    expect(project.ok && project.sdks.size() == 1 && project.boards.size() == 1,
           "platform definitions load from the ordinary manifest walk");
    expect(project.sdks.front().target == "arm-none-eabi" &&
               project.sdks.front().specs_profile == "rdimon",
           "SDK definition preserves target and profile");
    expect(project.boards.front().sdk == "arm-none-eabi-newlib" &&
               project.boards.front().sources.size() == 1,
           "board definition resolves its SDK and project source");
}

void validates_platform_keys_by_version_and_kind() {
    const mm::test::scoped_tree tree{"platform_key_gate"};
    tree.manifest_raw("",
                      "mm: 1.1\nkind: sdk\nname: old\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: newlib\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "platform keys require manifest version 1.2");

    tree.manifest_raw("",
                      "mm: 1.2\nkind: sdk\nname: bad\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: newlib\nfile: source.cpp\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "file is not valid on an SDK");

    tree.manifest_raw("",
                      "mm: 1.2\nkind: board\nname: bad\ntarget: arm-none-eabi\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "target is not valid on a board");

    tree.manifest_raw("",
                      "mm: 1.2\nkind: module\nname: bad\nmodule: bad\n"
                      "file: source.cpp\nrequires-board: mps2-an385\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "requires-board is not valid on a module");

    tree.manifest_raw("",
                      "mm: 1.1\nkind: board\nname: old\nsecurity-domain: secure\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "security-domain requires manifest version 1.2");

    tree.manifest_raw("",
                      "mm: 1.1\nkind: board\nname: old\nderives-from: mps2-an385\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "derives-from requires manifest version 1.2");

    tree.manifest_raw("",
                      "mm: 1.2\nkind: module\nname: bad\nmodule: bad\n"
                      "file: source.cpp\nderives-from: mps2-an385\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "derives-from is not valid on a module");
}

void rejects_bad_references_and_registry_values() {
    const mm::test::scoped_tree missing{"platform_missing_sdk"};
    make_platform_tree(missing);
    missing.manifest_raw("platforms/board",
                         "mm: 1.2\nkind: board\nname: mps2-an385\n"
                         "sdk: absent\ncpu: unknown\ninstruction-set: thumb\n"
                         "float-abi: soft\nlinker-script: link.ld\nfile: vectors.cpp\n");
    expect(!mm::build::load_project(missing.root()).ok,
           "a missing SDK reference is rejected before processor lookup");

    const mm::test::scoped_tree profile{"platform_unknown_profile"};
    make_platform_tree(profile);
    profile.manifest_raw("platforms/sdk",
                         "mm: 1.2\nkind: sdk\nname: arm-none-eabi-newlib\n"
                         "target: arm-none-eabi\ncompiler-family: gcc\nruntime: newlib\n"
                         "specs-profile: unknown\n");
    expect(!mm::build::load_project(profile.root()).ok,
           "an unknown specs profile is rejected without invoking a compiler");

    const mm::test::scoped_tree unknown_cpu{"platform_unknown_cpu"};
    make_platform_tree(unknown_cpu);
    unknown_cpu.manifest_raw("platforms/board",
                             "mm: 1.2\nkind: board\nname: mps2-an385\n"
                             "sdk: arm-none-eabi-newlib\ncpu: unknown\ninstruction-set: thumb\n"
                             "float-abi: soft\nlinker-script: link.ld\nfile: vectors.cpp\n");
    expect(!mm::build::load_project(unknown_cpu.root()).ok,
           "an unknown processor combination is rejected by the processor registry");
}

void accepts_registered_processor_combinations() {
    const mm::test::scoped_tree m0plus{"platform_cortex_m0plus"};
    make_platform_tree(m0plus);
    m0plus.manifest_raw("platforms/board",
                        "mm: 1.2\nkind: board\nname: rp2040-ram\n"
                        "sdk: arm-none-eabi-newlib\ncpu: cortex-m0plus\n"
                        "instruction-set: thumb\nfloat-abi: soft\n"
                        "machine: rp2040\nlinker-script: link.ld\n"
                        "file: vectors.cpp\nprovides: reset-vector\n"
                        "provides: initial-stack\nprovides: memory-layout\n");
    expect(mm::build::load_project(m0plus.root()).ok,
           "cortex-m0plus with thumb and soft is accepted");

    const mm::test::scoped_tree m33{"platform_cortex_m33"};
    make_platform_tree(m33);
    m33.manifest_raw("platforms/board",
                     "mm: 1.2\nkind: board\nname: rp2350-ram\n"
                     "sdk: arm-none-eabi-newlib\ncpu: cortex-m33\n"
                     "instruction-set: thumb\nfloat-abi: softfp\n"
                     "machine: rp2350\nlinker-script: link.ld\n"
                     "file: vectors.cpp\nprovides: reset-vector\n"
                     "provides: initial-stack\nprovides: memory-layout\n");
    const auto m33_project = mm::build::load_project(m33.root());
    expect(m33_project.ok, "cortex-m33 with thumb and softfp is accepted");
    expect(m33_project.boards.size() == 1 &&
               m33_project.boards.front().security_domain == "non-secure" &&
               m33_project.boards.front().compiler_arguments.size() == 3,
           "an omitted security domain preserves the non-secure Cortex-M33 arguments");

    const mm::test::scoped_tree secure_m33{"platform_secure_cortex_m33"};
    make_platform_tree(secure_m33);
    secure_m33.manifest_raw("platforms/board",
                            "mm: 1.2\nkind: board\nname: pico2\n"
                            "sdk: arm-none-eabi-newlib\ncpu: cortex-m33\n"
                            "instruction-set: thumb\nfloat-abi: softfp\n"
                            "security-domain: secure\nmachine: rp2350\n"
                            "linker-script: link.ld\nfile: vectors.cpp\n"
                            "provides: reset-vector\nprovides: initial-stack\n"
                            "provides: memory-layout\n");
    const auto secure_project = mm::build::load_project(secure_m33.root());
    expect(secure_project.ok && secure_project.boards.size() == 1 &&
               secure_project.boards.front().security_domain == "secure" &&
               secure_project.boards.front().compiler_arguments.size() == 4 &&
               secure_project.boards.front().compiler_arguments.back() == "-mcmse",
           "the secure Cortex-M33 registry entry carries its CMSE argument");

    secure_m33.manifest_raw("platforms/board",
                            "mm: 1.2\nkind: board\nname: bad\n"
                            "sdk: arm-none-eabi-newlib\ncpu: cortex-m33\n"
                            "instruction-set: thumb\nfloat-abi: softfp\n"
                            "security-domain: privileged\nlinker-script: link.ld\n"
                            "file: vectors.cpp\n");
    expect(!mm::build::load_project(secure_m33.root()).ok,
           "an unknown security domain is rejected");
}

void accepts_the_hazard3_processor_combination() {
    const mm::test::scoped_tree tree{"platform_hazard3"};
    make_platform_tree(tree);
    tree.manifest_raw("platforms/sdk",
                      "mm: 1.2\nkind: sdk\nname: pico-riscv\n"
                      "target: riscv32-pico-elf\ncompiler-family: gcc\nruntime: newlib\n");
    tree.manifest_raw("platforms/board",
                      "mm: 1.2\nkind: board\nname: pico2-riscv\n"
                      "sdk: pico-riscv\ncpu: hazard3\n"
                      "instruction-set: rv32imacb_zicsr_zifencei_zmmul_zaamo_zalrsc_"
                      "zca_zcb_zcmp_zba_zbb_zbkb_zbs_xh3bextm\n"
                      "float-abi: soft\nmachine: rp2350\nlinker-script: link.ld\n"
                      "file: vectors.cpp\nprovides: reset-vector\n"
                      "provides: initial-stack\nprovides: memory-layout\n"
                      "provides: runtime-init\nprovides: syscalls\n");
    const auto project = mm::build::load_project(tree.root());
    expect(project.ok, "riscv32-pico-elf is a registered bare-metal target");
    expect(project.boards.size() == 1 &&
               project.boards.front().compiler_arguments.size() == 2 &&
               project.boards.front().compiler_arguments[0] == "-mcpu=hazard3-rp2350" &&
               project.boards.front().compiler_arguments[1] == "-mstrict-align",
           "Hazard3 emits the SDK's measured preferred CPU profile and strict alignment");

    tree.manifest_raw("platforms/sdk",
                      "mm: 1.2\nkind: sdk\nname: pico-riscv\n"
                      "target: riscv64-unknown-elf\ncompiler-family: gcc\nruntime: newlib\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "an unregistered RISC-V triple is still rejected");
}

void external_directories_are_only_spelling_checked_by_the_walk() {
    const mm::test::scoped_tree tree{"platform_external_directory"};
    tree.manifest("", "kind: project\nname: p\nfolder: sdk\n");
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: m68k-linux-glibc\n"
                      "target: m68k-linux-gnu\ncompiler-family: gcc\nruntime: glibc\n"
                      "runtime-prefix: /directory/that/need/not/exist/here\n");
    expect(mm::build::load_project(tree.root()).ok,
           "the walk does not require an unselected SDK's machine directory");

    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: m68k-linux-glibc\n"
                      "target: m68k-linux-gnu\ncompiler-family: gcc\nruntime: glibc\n"
                      "runtime-prefix: relative\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "the walk rejects a relative machine directory");
}

const mm::test::case_ cases[] = {
    {"loads SDK and board definitions", &loads_sdk_and_board_definitions},
    {"validates platform keys by version and kind", &validates_platform_keys_by_version_and_kind},
    {"rejects bad references and registry values", &rejects_bad_references_and_registry_values},
    {"accepts registered processor combinations", &accepts_registered_processor_combinations},
    {"accepts the hazard3 processor combination", &accepts_the_hazard3_processor_combination},
    {"external directories are selection-scoped", &external_directories_are_only_spelling_checked_by_the_walk},
=======
    tree.manifest("boards", "kind: dir\nname: boards\n");
}

void derives_from_resolves_platform_identity() {
    const mm::test::scoped_tree tree{"board_identity_derivation"};
    make_base_tree(tree);

    tree.manifest("boards", "kind: dir\nname: boards\nfolder: custom\n");
    tree.manifest_raw("boards/custom",
                      "mm: 1.2\nkind: board\nname: custom-board\n"
                      "derives-from: mps2-an385\n");

    const auto project = mm::build::load_project(tree.root());
    expect(project.ok && project.boards.size() == 2, "derived board loads successfully");

    const mm::build::BoardDefinition* base = nullptr;
    const mm::build::BoardDefinition* derived = nullptr;
    for (const auto& b : project.boards) {
        if (b.name == "mps2-an385") base = &b;
        if (b.name == "custom-board") derived = &b;
    }
    expect(base != nullptr && derived != nullptr, "both base and derived board definitions exist");

    expect(base->chain.size() == 1 && base->chain.front() == "mps2-an385",
           "base board chain contains only itself");
    expect(derived->chain.size() == 2 && derived->chain[0] == "custom-board" &&
               derived->chain[1] == "mps2-an385",
           "derived board chain contains selected board followed by base");

    expect(derived->sdk == "arm-none-eabi-newlib", "derived board inherits SDK");
    expect(derived->cpu == "cortex-m3", "derived board inherits CPU");
    expect(derived->instruction_set == "thumb", "derived board inherits instruction-set");
    expect(derived->float_abi == "soft", "derived board inherits float-abi");
    expect(derived->security_domain == "non-secure", "derived board inherits security-domain");
    expect(derived->machine == "mps2-an385", "derived board inherits machine");
    expect(derived->linker_script == tree.root() / "platforms/board/link.ld",
           "derived board inherits linker-script");
    expect(derived->sources.size() == 1 &&
               derived->sources.front() == tree.root() / "platforms/board/vectors.cpp",
           "derived board inherits sources");
    expect(derived->provides.size() == 3, "derived board inherits provides");
    expect(derived->compiler_arguments.size() == 3 &&
               derived->compiler_arguments[0] == "-mcpu=cortex-m3" &&
               derived->compiler_arguments[1] == "-mthumb" &&
               derived->compiler_arguments[2] == "-mfloat-abi=soft",
           "derived board compiler arguments resolve from inherited processor identity");
}

void path_resolution_precedes_merging_fixture() {
    const mm::test::scoped_tree tree{"board_path_resolution_fixture"};
    make_base_tree(tree);

    tree.manifest("boards", "kind: dir\nname: boards\nfolder: widget\n");
    tree.manifest_raw("boards/widget",
                      "mm: 1.2\nkind: board\nname: widget-board\n"
                      "derives-from: mps2-an385\n"
                      "linker-script: link.ld\n"
                      "file: vectors.cpp\n"
                      "file: pins.cpp\n"
                      "file: shared.cpp\n");
    std::ofstream(tree.root() / "boards/widget/link.ld") << "/* widget link.ld */\n";
    std::ofstream(tree.root() / "boards/widget/vectors.cpp") << "int widget_vectors;\n";
    std::ofstream(tree.root() / "boards/widget/pins.cpp") << "int pins;\n";
    std::ofstream(tree.root() / "boards/widget/shared.cpp") << "int shared;\n";

    const auto project = mm::build::load_project(tree.root());
    expect(project.ok, "project loads when both manifests declare same relative link.ld and vectors.cpp");

    const mm::build::BoardDefinition* derived = nullptr;
    for (const auto& b : project.boards) {
        if (b.name == "widget-board") derived = &b;
    }
    expect(derived != nullptr, "derived board found");

    expect(derived->linker_script == tree.root() / "boards/widget/link.ld",
           "derived link.ld overrides base link.ld and resolves to derived directory");

    expect(derived->sources.size() == 4, "derived board carries base and derived sources");
    expect(derived->sources[0] == tree.root() / "platforms/board/vectors.cpp",
           "base vectors.cpp appears first and resolves to base directory");
    expect(derived->sources[1] == tree.root() / "boards/widget/vectors.cpp",
           "derived vectors.cpp appears second and resolves to derived directory");
    expect(derived->sources[2] == tree.root() / "boards/widget/pins.cpp",
           "derived pins.cpp appears third and resolves to derived directory");
    expect(derived->sources[3] == tree.root() / "boards/widget/shared.cpp",
           "derived shared.cpp appears fourth and resolves to derived directory");

    // Missing inherited file in base manifest fails naming the base manifest
    std::filesystem::remove(tree.root() / "platforms/board/vectors.cpp");
    std::stringstream captured;
    auto* prev = std::cerr.rdbuf(captured.rdbuf());
    const auto failed_project = mm::build::load_project(tree.root());
    std::cerr.rdbuf(prev);
    expect(!failed_project.ok, "missing base source file causes load_project failure");
    const auto diag = captured.str();
    expect(diag.find("platforms/board/mm.mdy") != std::string::npos &&
               diag.find("vectors.cpp") != std::string::npos,
           "diagnostic for missing inherited file names the declaring manifest");
}

void scalar_overrides_and_merges() {
    const mm::test::scoped_tree tree{"board_scalar_overrides"};
    make_base_tree(tree);

    tree.manifest("boards", "kind: dir\nname: boards\nfolder: b1\nfolder: b2\n");
    tree.manifest_raw("boards/b1",
                      "mm: 1.2\nkind: board\nname: custom-override\n"
                      "derives-from: mps2-an385\n"
                      "machine: custom-mach\n"
                      "linker-script: custom.ld\n"
                      "file: extra.cpp\n"
                      "provides: runtime-init\n"
                      "provides: syscalls\n");
    std::ofstream(tree.root() / "boards/b1/custom.ld") << "SECTIONS {}\n";
    std::ofstream(tree.root() / "boards/b1/extra.cpp") << "int extra;\n";

    tree.manifest_raw("boards/b2",
                      "mm: 1.2\nkind: board\nname: custom-keep\n"
                      "derives-from: mps2-an385\n");

    const auto project = mm::build::load_project(tree.root());
    expect(project.ok && project.boards.size() == 3, "boards load successfully");

    const mm::build::BoardDefinition* b1 = nullptr;
    const mm::build::BoardDefinition* b2 = nullptr;
    for (const auto& b : project.boards) {
        if (b.name == "custom-override") b1 = &b;
        if (b.name == "custom-keep") b2 = &b;
    }

    expect(b1->machine == "custom-mach", "declared machine overrides base machine");
    expect(b1->linker_script == tree.root() / "boards/b1/custom.ld",
           "declared linker-script overrides base linker-script");
    expect(b1->sources.size() == 2 &&
               b1->sources[0] == tree.root() / "platforms/board/vectors.cpp" &&
               b1->sources[1] == tree.root() / "boards/b1/extra.cpp",
           "sources are merged base first, derived second");
    expect(b1->provides.size() == 5, "provides is union across chain covering all 5 responsibilities");

    expect(b2->machine == "mps2-an385", "omitted machine keeps base machine");
    expect(b2->linker_script == tree.root() / "platforms/board/link.ld",
           "omitted linker-script keeps base linker-script");
}

void platform_provider_precedence() {
    const mm::test::scoped_tree tree{"board_provider_precedence"};
    tree.manifest("", "kind: project\nname: p\nfolder: iface\nfolder: timer\n"
                      "folder: sdk_mcu\nfolder: base_mcu\nfolder: derived_mcu\nfolder: base_timer\n"
                      "folder: sdk\nfolder: base_b\nfolder: derived_b\n");
    tree.manifest_raw("iface",
                      "mm: 1.2\nkind: module\nname: iface\n"
                      "module: mm.mcu\nplatform-interface:\nfile: mcu.cppm\n");
    tree.manifest_raw("timer",
                      "mm: 1.2\nkind: module\nname: timer\n"
                      "module: mm.timer\nplatform-interface:\nfile: timer.cppm\n");
    tree.manifest_raw("sdk_mcu",
                      "mm: 1.2\nkind: module\nname: sdk-mcu\n"
                      "module: platform.sdk.mcu\nuse: mm.mcu\nfile: sdk_mcu.cppm\n");
    tree.manifest_raw("base_mcu",
                      "mm: 1.2\nkind: module\nname: base-mcu\n"
                      "module: platform.base.mcu\nuse: mm.mcu\nfile: base_mcu.cppm\n");
    tree.manifest_raw("derived_mcu",
                      "mm: 1.2\nkind: module\nname: derived-mcu\n"
                      "module: platform.derived.mcu\nuse: mm.mcu\nfile: derived_mcu.cppm\n");
    tree.manifest_raw("base_timer",
                      "mm: 1.2\nkind: module\nname: base-timer\n"
                      "module: platform.base.timer\nuse: mm.timer\nfile: base_timer.cppm\n");
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: arm-none-eabi-newlib\n"
                      "target: arm-none-eabi\ncompiler-family: gcc\nruntime: newlib\n"
                      "specs-profile: rdimon\n"
                      "platform-provider: mm.mcu platform.sdk.mcu\n");
    tree.manifest_raw("base_b",
                      "mm: 1.2\nkind: board\nname: base-board\n"
                      "sdk: arm-none-eabi-newlib\ncpu: cortex-m3\n"
                      "instruction-set: thumb\nfloat-abi: soft\n"
                      "linker-script: link.ld\nfile: vectors.cpp\n"
                      "provides: reset-vector\nprovides: initial-stack\nprovides: memory-layout\n"
                      "platform-provider: mm.mcu platform.base.mcu\n"
                      "platform-provider: mm.timer platform.base.timer\n");
    tree.manifest_raw("derived_b",
                      "mm: 1.2\nkind: board\nname: derived-board\n"
                      "derives-from: base-board\n"
                      "platform-provider: mm.mcu platform.derived.mcu\n");

    std::ofstream(tree.root() / "iface/mcu.cppm") << "export module mm.mcu;\n";
    std::ofstream(tree.root() / "timer/timer.cppm") << "export module mm.timer;\n";
    std::ofstream(tree.root() / "sdk_mcu/sdk_mcu.cppm") << "export module platform.sdk.mcu;\n";
    std::ofstream(tree.root() / "base_mcu/base_mcu.cppm") << "export module platform.base.mcu;\n";
    std::ofstream(tree.root() / "derived_mcu/derived_mcu.cppm") << "export module platform.derived.mcu;\n";
    std::ofstream(tree.root() / "base_timer/base_timer.cppm") << "export module platform.base.timer;\n";
    std::ofstream(tree.root() / "base_b/link.ld") << "SECTIONS {}\n";
    std::ofstream(tree.root() / "base_b/vectors.cpp") << "int v;\n";

    const auto project = mm::build::load_project(tree.root());
    expect(project.ok, "project with derived provider bindings loads successfully");

    const mm::build::BoardDefinition* base = nullptr;
    const mm::build::BoardDefinition* derived = nullptr;
    for (const auto& b : project.boards) {
        if (b.name == "base-board") base = &b;
        if (b.name == "derived-board") derived = &b;
    }
    expect(base != nullptr && derived != nullptr, "both boards exist");

    expect(base->providers.size() == 2, "base board has two provider bindings");
    expect(derived->providers.size() == 2, "derived board flattens two provider bindings");

    const auto derived_mcu_binding = std::find_if(
        derived->providers.begin(), derived->providers.end(),
        [](const mm::build::PlatformProviderBinding& b) { return b.interface_module == "mm.mcu"; });
    expect(derived_mcu_binding != derived->providers.end() &&
               derived_mcu_binding->provider_module == "platform.derived.mcu",
           "derived board overrides mm.mcu binding with platform.derived.mcu");

    const auto derived_timer_binding = std::find_if(
        derived->providers.begin(), derived->providers.end(),
        [](const mm::build::PlatformProviderBinding& b) { return b.interface_module == "mm.timer"; });
    expect(derived_timer_binding != derived->providers.end() &&
               derived_timer_binding->provider_module == "platform.base.timer",
           "derived board inherits mm.timer binding from base board");

    expect(derived->declared_providers.size() == 1 &&
               derived->declared_providers[0].interface_module == "mm.mcu" &&
               derived->declared_providers[0].provider_module == "platform.derived.mcu",
           "declared_providers retains only the binding declared by derived manifest");

    mm::build::Platform with_derived;
    with_derived.sdk = "arm-none-eabi-newlib";
    with_derived.board = "derived-board";
    const auto providers = mm::build::platform_providers(project, true, &with_derived, "test");
    const auto* mcu = providers.binding("mm.mcu");
    expect(mcu != nullptr && mcu->provider_module == "platform.derived.mcu" &&
               mcu->owner == "derived-board" && mcu->from_board,
           "effective provider for mm.mcu is platform.derived.mcu");
    const auto* timer = providers.binding("mm.timer");
    expect(timer != nullptr && timer->provider_module == "platform.base.timer" &&
               timer->owner == "derived-board" && timer->from_board,
           "effective provider for mm.timer is platform.base.timer inherited from base board");
    expect(providers.declares_provider("platform.sdk.mcu") &&
               providers.declares_provider("platform.base.mcu") &&
               providers.declares_provider("platform.derived.mcu") &&
               providers.declares_provider("platform.base.timer"),
           "all declared provider modules are known");
    expect(!providers.selects_provider("platform.sdk.mcu") &&
               !providers.selects_provider("platform.base.mcu") &&
               providers.selects_provider("platform.derived.mcu") &&
               providers.selects_provider("platform.base.timer"),
           "sdk binding is shadowed by base which is shadowed by derived");
}

void multi_level_board_chain() {
    const mm::test::scoped_tree tree{"board_multi_level"};
    make_base_tree(tree);

    tree.manifest("boards", "kind: dir\nname: boards\nfolder: parent\nfolder: child\n");
    tree.manifest_raw("boards/parent",
                      "mm: 1.2\nkind: board\nname: parent-board\n"
                      "derives-from: mps2-an385\n"
                      "machine: parent-mach\n"
                      "file: p.cpp\n");
    std::ofstream(tree.root() / "boards/parent/p.cpp") << "int p;\n";

    tree.manifest_raw("boards/child",
                      "mm: 1.2\nkind: board\nname: child-board\n"
                      "derives-from: parent-board\n"
                      "file: c.cpp\n");
    std::ofstream(tree.root() / "boards/child/c.cpp") << "int c;\n";

    const auto project = mm::build::load_project(tree.root());
    expect(project.ok, "multi-level derivation loads successfully");

    const mm::build::BoardDefinition* child = nullptr;
    for (const auto& b : project.boards) {
        if (b.name == "child-board") child = &b;
    }
    expect(child != nullptr, "child board found");
    expect(child->chain.size() == 3 &&
               child->chain[0] == "child-board" &&
               child->chain[1] == "parent-board" &&
               child->chain[2] == "mps2-an385",
           "child board chain reflects full 3-element ancestry");
    expect(child->machine == "parent-mach", "child inherits machine from parent");
    expect(child->linker_script == tree.root() / "platforms/board/link.ld",
           "child inherits linker-script from grandparent");
    expect(child->sources.size() == 3 &&
               child->sources[0] == tree.root() / "platforms/board/vectors.cpp" &&
               child->sources[1] == tree.root() / "boards/parent/p.cpp" &&
               child->sources[2] == tree.root() / "boards/child/c.cpp",
           "sources concatenate in order: grandparent, parent, child");
}

void rejects_invalid_derivations() {
    // Unknown base board
    {
        const mm::test::scoped_tree tree{"board_unknown_base"};
        make_base_tree(tree);
        tree.manifest("boards", "kind: dir\nname: boards\nfolder: bad\n");
        tree.manifest_raw("boards/bad",
                          "mm: 1.2\nkind: board\nname: bad-board\n"
                          "derives-from: non-existent\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "unknown base board is rejected");
        const auto diag = captured.str();
        expect(diag.find("derives-from references unknown board: non-existent") != std::string::npos,
               "diagnostic names unknown base board");
        expect(diag.find("mps2-an385") != std::string::npos, "diagnostic lists available boards");
    }

    // Self derivation cycle (1-element)
    {
        const mm::test::scoped_tree tree{"board_self_cycle"};
        tree.manifest("", "kind: project\nname: p\nfolder: b\n");
        tree.manifest_raw("b",
                          "mm: 1.2\nkind: board\nname: self-board\n"
                          "derives-from: self-board\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "self derivation is rejected");
        const auto diag = captured.str();
        expect(diag.find("derives-from: cycle in the board chain:") != std::string::npos,
               "cycle diagnostic header is printed");
        expect(diag.find("<- repeats") != std::string::npos, "repeats marker is printed");
    }

    // 2-element cycle (a -> b -> a)
    {
        const mm::test::scoped_tree tree{"board_2_cycle"};
        tree.manifest("", "kind: project\nname: p\nfolder: a\nfolder: b\n");
        tree.manifest_raw("a",
                          "mm: 1.2\nkind: board\nname: board-a\n"
                          "derives-from: board-b\n");
        tree.manifest_raw("b",
                          "mm: 1.2\nkind: board\nname: board-b\n"
                          "derives-from: board-a\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "2-element board cycle is rejected");
        const auto diag = captured.str();
        expect(diag.find("derives-from: cycle in the board chain:") != std::string::npos,
               "cycle diagnostic header is printed");
        expect(diag.find("<- repeats") != std::string::npos, "repeats marker is printed");
    }

    // Repeated derives-from in single manifest
    {
        const mm::test::scoped_tree tree{"board_repeated_derives"};
        make_base_tree(tree);
        tree.manifest("boards", "kind: dir\nname: boards\nfolder: b\n");
        tree.manifest_raw("boards/b",
                          "mm: 1.2\nkind: board\nname: bad-board\n"
                          "derives-from: mps2-an385\n"
                          "derives-from: mps2-an385\n");
        expect(!mm::build::load_project(tree.root()).ok,
               "repeated derives-from in single manifest is rejected");
    }

    // Redeclaring immutable keys
    const char* const immutable_keys[] = {
        "sdk: arm-none-eabi-newlib",
        "cpu: cortex-m3",
        "instruction-set: thumb",
        "float-abi: soft",
        "security-domain: non-secure",
    };
    for (const char* key_entry : immutable_keys) {
        const mm::test::scoped_tree tree{"board_redeclare"};
        make_base_tree(tree);
        tree.manifest("boards", "kind: dir\nname: boards\nfolder: b\n");
        tree.manifest_raw("boards/b",
                          std::string("mm: 1.2\nkind: board\nname: custom-board\n"
                                      "derives-from: mps2-an385\n") +
                              key_entry + "\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "redeclaration of immutable key is rejected");
        const auto diag = captured.str();
        expect(diag.find("cannot redeclare") != std::string::npos,
               "redeclaration diagnostic indicates cannot redeclare");
        expect(diag.find("platforms/board/mm.mdy") != std::string::npos &&
                   diag.find("boards/b/mm.mdy") != std::string::npos,
               "redeclaration diagnostic names both manifests");
    }

    // Duplicate responsibility in derivation chain
    {
        const mm::test::scoped_tree tree{"board_dup_responsibility"};
        make_base_tree(tree);
        tree.manifest("boards", "kind: dir\nname: boards\nfolder: b\n");
        tree.manifest_raw("boards/b",
                          "mm: 1.2\nkind: board\nname: custom-board\n"
                          "derives-from: mps2-an385\n"
                          "provides: reset-vector\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "duplicate responsibility in board chain is rejected");
        const auto diag = captured.str();
        expect(diag.find("reset-vector") != std::string::npos,
               "diagnostic names the duplicated responsibility");
        expect(diag.find("platforms/board/mm.mdy") != std::string::npos &&
                   diag.find("boards/b/mm.mdy") != std::string::npos,
               "duplicate responsibility diagnostic names both manifests");
    }
}

void rejects_linker_script_on_externally_linked_board() {
    const mm::test::scoped_tree tree{"board_ext_linker_script"};
    tree.manifest("", "kind: project\nname: p\nfolder: library\nfolder: sdk\nfolder: boards\n");
    std::filesystem::create_directories(tree.root() / "library");
    std::ofstream(tree.root() / "library/LICENSE") << "licence\n";
    std::filesystem::create_directories(tree.root() / "library/cmake");
    std::ofstream(tree.root() / "library/cmake/CMakeLists.txt") << "# cmake\n";
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: pico-sdk\nsource: third_party\n"
                      "licence: LICENSE\nexternal-build: cmake\n");
    tree.manifest_raw("sdk",
                      "mm: 1.2\nkind: sdk\nname: pico-arm\ntarget: arm-none-eabi\n"
                      "compiler-family: gcc\nruntime: none\nlibrary: pico-sdk\n"
                      "provides: reset-vector\nprovides: initial-stack\n"
                      "provides: memory-layout\nprovides: runtime-init\nprovides: syscalls\n");

    // Case 1: Direct board declares linker-script
    tree.manifest("boards", "kind: dir\nname: boards\nfolder: direct\n");
    tree.manifest_raw("boards/direct",
                      "mm: 1.2\nkind: board\nname: direct-board\nsdk: pico-arm\n"
                      "cpu: cortex-m0plus\ninstruction-set: thumb\nfloat-abi: soft\n"
                      "linker-script: link.ld\n");
    std::ofstream(tree.root() / "boards/direct/link.ld") << "SECTIONS {}\n";
    {
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "direct externally linked board declaring linker-script is rejected");
        const auto diag = captured.str();
        expect(diag.find("boards/direct/mm.mdy") != std::string::npos,
               "diagnostic names declaring manifest");
        expect(diag.find("pico-arm") != std::string::npos,
               "diagnostic names external SDK");
        expect(diag.find("cannot declare linker-script") != std::string::npos,
               "diagnostic indicates cannot declare linker-script");
    }

    // Case 2: Base board without linker script, derived board declares linker script
    tree.manifest_raw("boards/direct",
                      "mm: 1.2\nkind: board\nname: pico-base\nsdk: pico-arm\n"
                      "cpu: cortex-m0plus\ninstruction-set: thumb\nfloat-abi: soft\n");
    tree.manifest("boards", "kind: dir\nname: boards\nfolder: direct\nfolder: derived\n");
    tree.manifest_raw("boards/derived",
                      "mm: 1.2\nkind: board\nname: custom-pico\n"
                      "derives-from: pico-base\nlinker-script: link.ld\n");
    std::ofstream(tree.root() / "boards/derived/link.ld") << "SECTIONS {}\n";
    {
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "derived externally linked board declaring linker-script is rejected");
        const auto diag = captured.str();
        expect(diag.find("boards/derived/mm.mdy") != std::string::npos,
               "diagnostic names derived declaring manifest");
        expect(diag.find("pico-arm") != std::string::npos,
               "diagnostic names inherited external SDK");
        expect(diag.find("cannot declare linker-script") != std::string::npos,
               "diagnostic indicates cannot declare linker-script");
    }

    // Case 3: Derived board declaring file (board source) without linker script succeeds
    tree.manifest_raw("boards/derived",
                      "mm: 1.2\nkind: board\nname: custom-pico\n"
                      "derives-from: pico-base\nfile: pins.cpp\n");
    std::ofstream(tree.root() / "boards/derived/pins.cpp") << "int p;\n";
    {
        const auto project = mm::build::load_project(tree.root());
        expect(project.ok, "derived externally linked board declaring file succeeds");
        const auto* custom = [&]() -> const mm::build::BoardDefinition* {
            for (const auto& b : project.boards)
                if (b.name == "custom-pico") return &b;
            return nullptr;
        }();
        expect(custom != nullptr, "custom-pico board present");
        if (custom != nullptr) {
            expect(custom->sources.size() == 1, "custom-pico has 1 source");
            expect(custom->linker_script.empty(), "custom-pico has no linker script");
        }
    }
}

void diagnostics_name_supplying_manifest() {
    // 1. Inherited unknown SDK
    {
        const mm::test::scoped_tree tree{"board_diag_unknown_sdk"};
        tree.manifest("", "kind: project\nname: p\nfolder: base\nfolder: derived\n");
        tree.manifest_raw("base",
                          "mm: 1.2\nkind: board\nname: base-board\nsdk: nonexistent-sdk\n"
                          "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n"
                          "linker-script: link.ld\nfile: v.cpp\n"
                          "provides: reset-vector\nprovides: initial-stack\nprovides: memory-layout\n"
                          "provides: runtime-init\nprovides: syscalls\n");
        std::ofstream(tree.root() / "base/link.ld") << "SECTIONS {}\n";
        std::ofstream(tree.root() / "base/v.cpp") << "int v;\n";
        tree.manifest_raw("derived",
                          "mm: 1.2\nkind: board\nname: derived-board\n"
                          "derives-from: base-board\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "unknown SDK is rejected");
        const auto diag = captured.str();
        expect(diag.find("base/mm.mdy") != std::string::npos,
               "unknown SDK diagnostic names base manifest");
    }

    // 2. Missing linker script on derived board
    {
        const mm::test::scoped_tree tree{"board_diag_missing_ld"};
        tree.manifest("", "kind: project\nname: p\nfolder: sdk\nfolder: base\nfolder: derived\n");
        tree.manifest_raw("sdk",
                          "mm: 1.2\nkind: sdk\nname: test-sdk\ntarget: arm-none-eabi\n"
                          "compiler-family: gcc\nruntime: newlib\nspecs-profile: rdimon\n");
        tree.manifest_raw("base",
                          "mm: 1.2\nkind: board\nname: base-board\nsdk: test-sdk\n"
                          "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n"
                          "file: v.cpp\n"
                          "provides: reset-vector\nprovides: initial-stack\nprovides: memory-layout\n");
        std::ofstream(tree.root() / "base/v.cpp") << "int v;\n";
        tree.manifest_raw("derived",
                          "mm: 1.2\nkind: board\nname: derived-board\n"
                          "derives-from: base-board\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "missing linker script is rejected");
        const auto diag = captured.str();
        expect(diag.find("board requires linker-script") != std::string::npos,
               "diagnostic mentions board requires linker-script");
    }

    // 3. Missing files on derived board
    {
        const mm::test::scoped_tree tree{"board_diag_missing_files"};
        tree.manifest("", "kind: project\nname: p\nfolder: sdk\nfolder: base\nfolder: derived\n");
        tree.manifest_raw("sdk",
                          "mm: 1.2\nkind: sdk\nname: test-sdk\ntarget: arm-none-eabi\n"
                          "compiler-family: gcc\nruntime: newlib\nspecs-profile: rdimon\n");
        tree.manifest_raw("base",
                          "mm: 1.2\nkind: board\nname: base-board\nsdk: test-sdk\n"
                          "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n"
                          "linker-script: link.ld\n"
                          "provides: reset-vector\nprovides: initial-stack\nprovides: memory-layout\n");
        std::ofstream(tree.root() / "base/link.ld") << "SECTIONS {}\n";
        tree.manifest_raw("derived",
                          "mm: 1.2\nkind: board\nname: derived-board\n"
                          "derives-from: base-board\n");
        std::stringstream captured;
        auto* prev = std::cerr.rdbuf(captured.rdbuf());
        const auto project = mm::build::load_project(tree.root());
        std::cerr.rdbuf(prev);
        expect(!project.ok, "missing files is rejected");
        const auto diag = captured.str();
        expect(diag.find("board requires at least one file") != std::string::npos,
               "diagnostic mentions board requires at least one file");
    }
}

void requires_board_exact_matching() {
    const mm::test::scoped_tree tree{"requires_board_exact"};
    make_base_tree(tree);
    tree.manifest("boards", "kind: dir\nname: boards\nfolder: custom\n");
    tree.manifest_raw("boards/custom",
                      "mm: 1.2\nkind: board\nname: custom-board\n"
                      "derives-from: mps2-an385\n");
    tree.manifest_raw("app",
                      "mm: 1.2\nkind: app\nname: board-app\n"
                      "file: main.cpp\nrequires-board: mps2-an385\n");
    std::ofstream(tree.root() / "app/main.cpp") << "int main() {}\n";

    tree.manifest("", "kind: project\nname: p\nfolder: platforms\nfolder: boards\nfolder: app\n");

    const auto project = mm::build::load_project(tree.root());
    expect(project.ok, "project loads");

    std::size_t app_node = 0;
    for (std::size_t i = 0; i < project.nodes.size(); ++i) {
        if (project.nodes[i].name == "board-app") app_node = i;
    }

    // 1. When base board is selected, app is available
    mm::build::Platform base_platform;
    base_platform.board = "mps2-an385";
    const auto avail_base = mm::build::availability(
        project, app_node, true, true, &base_platform);
    expect(avail_base.available, "app is available when matching base board is selected");

    // 2. When derived board is selected, app is NOT available (exact match required)
    mm::build::Platform derived_platform;
    derived_platform.board = "custom-board";
    derived_platform.board_derives_from = {"mps2-an385"};
    const auto avail_derived = mm::build::availability(
        project, app_node, true, true, &derived_platform);
    expect(!avail_derived.available, "app is unavailable when derived board is selected");
    expect(avail_derived.reason.find("requires board mps2-an385") != std::string::npos,
           "unavailable reason indicates required board");
    expect(avail_derived.reason.find("selected board is custom-board") != std::string::npos,
           "unavailable reason indicates selected board");
}

const mm::test::case_ cases[] = {
    {"library definition", &library_definition},
    {"library source boundary", &source_boundary},
    {"module library reference", &module_reference_validation},
    {"SDK library reference", &sdk_reference_and_validation},
    {"external-build validation", &external_build_validation},
    {"external-build SDK and board rules", &external_build_sdk_and_board_rules},
    {"external-wrapper availability", &external_wrapper_availability},

    {"platform-interface grammar", &marker_grammar},
    {"platform-provider grammar and references", &provider_grammar_and_references},
    {"board over SDK precedence", &precedence_and_selection},
    {"interface requirements", &requirements_follow_the_authored_closure},
    {"nested interface requirements", &requirements_follow_selected_provider_closures},
    {"unmet requirement diagnostics", &an_unmet_requirement_names_the_platform},
    {"unbuildable selected provider",
     &an_unbuildable_selected_provider_makes_the_executable_unavailable},
    {"providers are not roots", &a_provider_is_never_an_independent_root},

    {"derives-from resolves platform identity", &derives_from_resolves_platform_identity},
    {"path resolution precedes merging fixture", &path_resolution_precedes_merging_fixture},
    {"scalar overrides and merges", &scalar_overrides_and_merges},
    {"platform provider precedence", &platform_provider_precedence},
    {"multi-level board chain", &multi_level_board_chain},
    {"rejects invalid derivations", &rejects_invalid_derivations},
    {"rejects linker script on externally linked board", &rejects_linker_script_on_externally_linked_board},
    {"diagnostics name supplying manifest", &diagnostics_name_supplying_manifest},
    {"requires-board exact matching", &requires_board_exact_matching},
>>>>>>> f0dd29c (test: group provider and board tests under mm.build platform)
};

const mm::test::registrar reg{"mm.build platform", cases};

<<<<<<< HEAD
}
=======
}  // namespace
>>>>>>> f0dd29c (test: group provider and board tests under mm.build platform)
