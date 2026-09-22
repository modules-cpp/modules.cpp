// Platform-side tests: provider and board selection, availability, library
// checkout validation and include directories.
//
// Selection, availability, and provider binding are exercised through
// loaded manifest trees and, where the API allows, pure functions over a
// Project. Board derivation and name grammar live in the board suite.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
import mm.configure;
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

    // Every sketch application holds the generated compatibility header,
    // because its main.cpp includes it, so its own directory is on the
    // include path whether or not it reaches a sketch library.
    auto sketch_app = unrelated;
    sketch_app.sketches = {"app.ino"};
    sketch_app.source_dir = tree.root() / "apps/app";
    expect(mm::build::library_include_directories(
               tree.root(), project.libraries, sketch_app, includes) &&
               includes.size() == 1 && includes.front() == sketch_app.source_dir,
           "a sketch application without a library still names its own directory");
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


// --- :platform API tests beyond provider selection

void can_link_executable_reports_unresolved_responsibilities() {
    mm::build::Platform platform;
    expect(mm::build::can_link_executable(&platform, "build", "app"),
           "a platform without unresolved responsibilities links");
    expect(mm::build::can_link_executable(nullptr, "build", "app"),
           "a host lane without a platform links");
    platform.models_responsibilities = true;
    platform.unresolved.push_back(mm::configure::Responsibility::Syscalls);
    expect(!mm::build::can_link_executable(&platform, "build", "app"),
           "an unresolved responsibility blocks the link");
}

void configuration_nodes_follow_the_project_walk() {
    const mm::test::scoped_tree tree{"platform_confignodes"};
    tree.manifest("", "kind: project\nname: p\nfolder: mods\n");
    tree.manifest("mods", "kind: dir\nname: mods\nfolder: a\nfolder: b\n");
    tree.manifest("mods/a", "kind: module\nname: a\nmodule: mm.a\nfile: a.cppm\n");
    tree.manifest("mods/b", "kind: module\nname: b\nmodule: mm.b\nfile: b.cppm\n");
    const auto project = mm::build::load_project(tree.root());
    expect(project.ok, "the project loads");
    const auto nodes = mm::build::configuration_nodes(project);
    expect(nodes.size() == project.nodes.size(),
           "one configuration node per walked node");
    for (std::size_t i = 0; i < nodes.size(); ++i)
        expect(nodes[i].name == project.nodes[i].name,
               "configuration nodes stay parallel to the walk");
}

void structural_validation_enforces_lanes_core_and_library() {
    auto node = [](std::string_view name, std::string_view module,
                   const std::vector<std::string>& uses) {
        mm::configure::OptionNode n;
        n.manifest = "mm.mdy";
        n.name = std::string(name);
        n.kind = "module";
        n.module_name = std::string(module);
        n.uses = uses;
        return n;
    };
    const auto nodes = {node("a", "mm.a", {}), node("b", "mm.b", {"mm.a"})};

    mm::build::StructuralProperties properties;
    properties.nodes.resize(nodes.size());
    expect(mm::build::validate_structural_properties(nodes, properties, "test"),
           "default capabilities and core status validate");

    mm::build::StructuralProperties short_properties;
    short_properties.nodes.resize(1);
    expect(!mm::build::validate_structural_properties(nodes, short_properties, "test"),
           "a properties vector shorter than the walk is rejected");

    properties.nodes[0].buildable_host.value = false;
    properties.nodes[0].buildable_host.value_source = "mods/a/mm.mdy";
    expect(!mm::build::validate_structural_properties(nodes, properties, "test"),
           "a host-only consumer cannot use a host-unbuildable module");

    properties.nodes[0].buildable_host.value = true;
    properties.nodes[0].core.value = false;
    expect(!mm::build::validate_structural_properties(nodes, properties, "test"),
           "a core consumer cannot use a non-core module");
}

void load_test_loads_a_test_manifest() {
    const mm::test::scoped_tree tree{"platform_loadtest"};
    tree.manifest("", "kind: project\nname: p\nfolder: tests\n");
    tree.manifest("tests", "kind: dir\nname: tests\nfolder: t\n");
    tree.manifest("tests/t", "kind: test\nname: t\nunit: t.cpp\n");
    tree.manifest("tests/t/wrong", "kind: app\nname: wrong\nfile: w.cpp\n");

    bool ok = false;
    const auto target = mm::build::load_test(tree.root() / "tests" / "t" / "mm.mdy", ok);
    expect(ok, "a kind:test manifest loads");
    expect(target.kind == "test" && target.name == "t", "the target records its identity");
    expect(target.sources.size() == 1 && target.sources.front().path == "t.cpp",
           "the declared unit becomes the target source");

    ok = true;
    mm::build::load_test(tree.root() / "tests" / "t" / "wrong" / "mm.mdy", ok);
    expect(!ok, "a non-test manifest fails the load");
    ok = true;
    mm::build::load_test(tree.root() / "tests" / "missing" / "mm.mdy", ok);
    expect(!ok, "an absent manifest fails the load");
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

    {"can link executable reports unresolved responsibilities", &can_link_executable_reports_unresolved_responsibilities},
    {"configuration nodes follow the project walk", &configuration_nodes_follow_the_project_walk},
    {"structural validation enforces lanes core and library", &structural_validation_enforces_lanes_core_and_library},
    {"load test loads a test manifest", &load_test_loads_a_test_manifest},
};

const mm::test::registrar reg{"mm.build platform", cases};

}  // namespace
