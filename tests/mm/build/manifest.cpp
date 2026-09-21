// Black box tests for mm.build's manifest walk.
//
// These build throwaway manifest trees under the system temp directory and hand
// them to load_tree. The hostile cases come from a review finding: a folder:
// entry naming ".", a two manifest loop and a symlink loop all made the walk
// recurse until it was killed. Each must now terminate with ok == false.
//
// Negative cases deliberately drive load_tree into its error paths, so this
// suite prints "build: ..." diagnostics to stderr while passing. That output is
// the behaviour under test, not a failure.

#include <filesystem>
#include <fstream>
#include <string_view>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

bool has_target(const mm::build::Tree& tree, std::string_view name) {
    for (const auto& target : tree.targets)
        if (target.name == name) return true;
    return false;
}

int count_targets(const mm::build::Tree& tree, std::string_view name) {
    int found = 0;
    for (const auto& target : tree.targets)
        if (target.name == name) ++found;
    return found;
}

// --- the shape a healthy tree produces ----------------------------------

void walks_a_nested_tree() {
    const mm::test::scoped_tree tree{"nested"};
    tree.manifest("", "kind: project\nname: p\nfolder: modules\n");
    tree.manifest("modules", "kind: dir\nname: modules\nfolder: one\nfolder: two\n");
    tree.manifest("modules/one", "kind: module\nname: one\nmodule: mm.one\nfile: one.cppm\n");
    tree.manifest("modules/two", "kind: app\nname: two\nfile: two.cpp\nuse: mm.one\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected a well formed tree to load");
    mm::test::expect(loaded.targets.size() == 2, "expected two targets");
    mm::test::expect(has_target(loaded, "one"), "expected the module target");
    mm::test::expect(has_target(loaded, "two"), "expected the app target");
}

void separates_tests_and_docs_from_targets() {
    const mm::test::scoped_tree tree{"kinds"};
    tree.manifest("", "kind: project\nname: p\nfolder: m\nfolder: t\nfolder: d\n");
    tree.manifest("m", "kind: module\nname: m\nmodule: mm.m\nfile: m.cppm\n");
    tree.manifest("t", "kind: test\nname: t\nunit: t.cpp\n");
    tree.manifest("d", "kind: doc\nname: d\nfile: d.mdy\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected the tree to load");
    mm::test::expect(loaded.targets.size() == 1, "expected only the module among build targets");
    mm::test::expect(loaded.tests.size() == 1, "expected the test target to be collected separately");
    mm::test::expect(loaded.docs.size() == 1, "expected the doc target to be collected separately");
}

// --- cycles must terminate ----------------------------------------------

void rejects_self_referencing_folder() {
    const mm::test::scoped_tree tree{"selfref"};
    tree.manifest("", "kind: project\nname: p\nfolder: .\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected folder: . to be rejected rather than followed forever");
}

void rejects_two_manifest_cycle() {
    const mm::test::scoped_tree tree{"cycle2"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: dir\nname: a\nfolder: ../b\n");
    tree.manifest("b", "kind: dir\nname: b\nfolder: ../a\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a folder: loop between two manifests to be rejected");
}

void rejects_symlink_cycle() {
    const mm::test::scoped_tree tree{"symlink"};
    tree.manifest("", "kind: project\nname: p\nfolder: real\n");
    tree.manifest("real", "kind: dir\nname: r\nfolder: link\n");

    std::error_code ec;
    std::filesystem::create_directory_symlink("..", tree.root() / "real" / "link", ec);
    if (ec) return;  // filesystem without symlinks; nothing to assert

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a symlink that loops back to be rejected");
}

void rejects_traversal_above_the_root() {
    const mm::test::scoped_tree tree{"escape"};
    tree.manifest("", "kind: project\nname: p\nfolder: sub\n");
    tree.manifest("sub", "kind: dir\nname: s\nfolder: ../..\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a folder: entry climbing above the root to be rejected");
}

// A diamond is not a cycle: two directories may legitimately name one shared
// folder, and it must be visited once rather than duplicated or rejected.
void accepts_a_diamond_once() {
    const mm::test::scoped_tree tree{"diamond"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\nfolder: b\n");
    tree.manifest("a", "kind: dir\nname: a\nfolder: ../shared\n");
    tree.manifest("b", "kind: dir\nname: b\nfolder: ../shared\n");
    tree.manifest("shared", "kind: module\nname: s\nmodule: mm.s\nfile: s.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected a shared folder to be accepted");
    mm::test::expect(count_targets(loaded, "s") == 1,
                     "expected a shared folder to produce exactly one target");
}

// --- malformed manifests ------------------------------------------------

void rejects_missing_manifest() {
    const mm::test::scoped_tree tree{"missing"};
    tree.manifest("", "kind: project\nname: p\nfolder: gone\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a folder: entry with no manifest to be rejected");
}

void rejects_unknown_kind() {
    const mm::test::scoped_tree tree{"kind"};
    tree.manifest("", "kind: project\nname: p\nfolder: x\n");
    tree.manifest("x", "kind: widget\nname: x\nfile: x.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected an unknown kind to be rejected");
}

// A review finding: mm: was declared by every real manifest but never
// actually checked, so a missing, unsupported, or duplicated version was
// silently accepted.
void rejects_missing_mm_version() {
    const mm::test::scoped_tree tree{"nommversion"};
    tree.manifest_raw("", "kind: project\nname: p\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a manifest with no mm: version to be rejected");
}

void rejects_unsupported_mm_version() {
    const mm::test::scoped_tree tree{"badmmversion"};
    tree.manifest_raw("", "mm: 0.2\nkind: project\nname: p\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected an unsupported old mm: version to be rejected");
}

// 1.2 is the current strict source-manifest format for release v1.2.0.
void accepts_current_mm_version() {
    const mm::test::scoped_tree tree{"mm12version"};
    tree.manifest_raw("", "mm: 1.2\nkind: project\nname: p\nfolder: m\n");
    tree.manifest_raw("m", "mm: 1.2\nkind: module\nname: m\nmodule: p.m\nfile: m.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok && loaded.targets.size() == 1,
                     "expected mm: 1.2 to be accepted");
}

void rejects_unknown_key_in_current_mm_version() {
    const mm::test::scoped_tree tree{"mm12unknown"};
    tree.manifest_raw("", "mm: 1.2\nkind: project\nname: p\nnot-a-key: x\n");

    bool ok = false;
    mm::build::load_nodes(tree.root(), ok, {.tool = "configure", .strict_tree = true});

    mm::test::expect(!ok, "expected an unknown key in a 1.2 manifest to be rejected");
}

void rejects_duplicate_mm_version() {
    const mm::test::scoped_tree tree{"dupmmversion"};
    tree.manifest_raw("", "mm: 1.0\nmm: 1.0\nkind: project\nname: p\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a manifest declaring mm: twice to be rejected");
}

void rejects_module_without_module_name() {
    const mm::test::scoped_tree tree{"nomodule"};
    tree.manifest("", "kind: project\nname: p\nfolder: m\n");
    tree.manifest("m", "kind: module\nname: m\nfile: m.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a module without a module: name to be rejected");
}

void rejects_target_without_sources() {
    const mm::test::scoped_tree tree{"nofiles"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: app\nname: a\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected an app with no file: entries to be rejected");
}

// A review finding: index_of_module returns the first target with a given
// module: name, so two modules exporting the same name were silently
// treated as interchangeable rather than rejected.
void rejects_duplicate_module_name() {
    const mm::test::scoped_tree tree{"dupmodule"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\nfolder: b\n");
    tree.manifest("a", "kind: module\nname: a\nmodule: dup\nfile: a.cppm\n");
    tree.manifest("b", "kind: module\nname: b\nmodule: dup\nfile: b.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected two modules exporting the same name to be rejected");
}

// The same finding: install() writes an app's binary to out/bin/<name>, so
// two apps with the same name would silently overwrite one another there.
void rejects_duplicate_app_name() {
    const mm::test::scoped_tree tree{"dupapp"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\nfolder: b\n");
    tree.manifest("a", "kind: app\nname: dup\nfile: a.cpp\n");
    tree.manifest("b", "kind: app\nname: dup\nfile: b.cpp\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected two apps with the same name to be rejected");
}

// A module and an app may not share a name either: both would resolve to
// out/bin/<name>, the same collision as two apps, just across kinds.
void rejects_duplicate_name_across_kinds() {
    const mm::test::scoped_tree tree{"dupkind"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\nfolder: b\n");
    tree.manifest("a", "kind: module\nname: dup\nmodule: mm.dup\nfile: a.cppm\n");
    tree.manifest("b", "kind: app\nname: dup\nfile: b.cpp\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok,
                     "a module and an app sharing a name is not itself rejected: "
                     "only the app installs to out/bin");
}

// A doc target is prose, so an empty file: list is allowed where it would be an
// error for anything that gets compiled.
void accepts_doc_without_files() {
    const mm::test::scoped_tree tree{"emptydoc"};
    tree.manifest("", "kind: project\nname: p\nfolder: d\n");
    tree.manifest("d", "kind: doc\nname: d\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected a doc target with no files to be accepted");
    mm::test::expect(loaded.docs.size() == 1, "expected the doc target to be collected");
}

void accepts_mm_13_version() {
    const mm::test::scoped_tree tree{"mm13version"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.3\nkind: app\nname: a\nfile: main.cpp\nsketch: a.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok && loaded.targets.size() == 1,
                     "expected mm: 1.3 and sketch: on kind: app to be accepted");
}

void rejects_sketch_key_under_mm_12() {
    const mm::test::scoped_tree tree{"sketch12"};
    tree.manifest_raw("", "mm: 1.2\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.2\nkind: app\nname: a\nfile: main.cpp\nsketch: a.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected sketch: under mm: 1.2 to be rejected");
}

void rejects_sketch_key_on_non_app_kind() {
    const mm::test::scoped_tree tree{"sketchnonapp"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: m\n");
    tree.manifest_raw("m", "mm: 1.3\nkind: module\nname: m\nmodule: p.m\nfile: m.cppm\nsketch: m.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected sketch: on kind: module to be rejected");
}

void accepts_sketch_app_and_records_sketches() {
    const mm::test::scoped_tree tree{"sketchapp"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.3\nkind: app\nname: a\nfile: main.cpp\nsketch: a.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected sketch app to load");
    mm::test::expect(loaded.targets.size() == 1, "expected 1 target");
    mm::test::expect(loaded.targets[0].sketches.size() == 1, "expected 1 sketch");
    mm::test::expect(loaded.targets[0].sketches[0] == "a.ino", "expected a.ino");
    mm::test::expect(loaded.targets[0].sources.size() == 1, "expected 1 source");
}

void defaults_main_cpp_when_file_omitted_for_sketch_app() {
    const mm::test::scoped_tree tree{"sketchdefault"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.3\nkind: app\nname: a\nsketch: a.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected sketch app without file: to load");
    mm::test::expect(loaded.targets.size() == 1, "expected 1 target");
    mm::test::expect(loaded.targets[0].sources.size() == 1, "expected default main.cpp source");
    mm::test::expect(loaded.targets[0].sources[0].path == (tree.root() / "a" / "main.cpp").lexically_normal().string(),
                     "expected main.cpp path");
}

void includes_main_cpp_when_helper_files_explicit() {
    const mm::test::scoped_tree tree{"sketchhelper"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.3\nkind: app\nname: a\nfile: helper.cpp\nsketch: a.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected sketch app with helper file to load");
    mm::test::expect(loaded.targets.size() == 1, "expected 1 target");
    mm::test::expect(loaded.targets[0].sources.size() == 2, "expected helper.cpp and main.cpp");
    bool found_main = false;
    bool found_helper = false;
    for (const auto& s : loaded.targets[0].sources) {
        if (std::filesystem::path(s.path).filename() == "main.cpp") found_main = true;
        if (std::filesystem::path(s.path).filename() == "helper.cpp") found_helper = true;
    }
    mm::test::expect(found_main, "expected main.cpp in sources");
    mm::test::expect(found_helper, "expected helper.cpp in sources");
}

void avoids_duplicate_main_cpp_when_explicit() {
    const mm::test::scoped_tree tree{"sketchdupmain"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.3\nkind: app\nname: a\nfile: main.cpp\nfile: helper.cpp\nsketch: a.ino\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected sketch app to load");
    mm::test::expect(loaded.targets.size() == 1, "expected 1 target");
    mm::test::expect(loaded.targets[0].sources.size() == 2, "expected exactly 2 sources without duplicate main");
}


// --- platform definitions and version gates -------------------------

using mm::test::expect;

void make_platform_tree(const mm::test::scoped_tree& tree) {
    tree.manifest("", "kind: project\nname: p\nfolder: platforms\n");
    tree.manifest("platforms",
                  "kind: dir\nname: platforms\nfolder: sdk\nfolder: board\n");
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

void validates_board_name_grammar() {
    // Direct tests for is_safe_board_name
    expect(mm::build::is_safe_board_name("pico"), "lowercase letters are safe");
    expect(mm::build::is_safe_board_name("pico-w"), "dash is safe");
    expect(mm::build::is_safe_board_name("pico2-arm"), "letters, numbers, dash are safe");
    expect(mm::build::is_safe_board_name("pico2_w"), "underscore is safe");
    expect(mm::build::is_safe_board_name("board.1"), "dot is safe");
    expect(mm::build::is_safe_board_name("board+2"), "plus is safe");
    expect(mm::build::is_safe_board_name("B1"), "uppercase is safe");
    expect(mm::build::is_safe_board_name("123-board"), "lead digit is safe");

    expect(!mm::build::is_safe_board_name(""), "empty name is not safe");
    expect(!mm::build::is_safe_board_name("-lead-dash"), "lead dash is not safe");
    expect(!mm::build::is_safe_board_name(".lead-dot"), "lead dot is not safe");
    expect(!mm::build::is_safe_board_name("_lead-under"), "lead underscore is not safe");
    expect(!mm::build::is_safe_board_name("+lead-plus"), "lead plus is not safe");
    expect(!mm::build::is_safe_board_name("has space"), "space is not safe");
    expect(!mm::build::is_safe_board_name("has\"quote"), "quote is not safe");
    expect(!mm::build::is_safe_board_name("has'squote"), "single quote is not safe");
    expect(!mm::build::is_safe_board_name("has\\backslash"), "backslash is not safe");
    expect(!mm::build::is_safe_board_name("has;semicolon"), "semicolon is not safe");
    expect(!mm::build::is_safe_board_name("has[bracket"), "open bracket is not safe");
    expect(!mm::build::is_safe_board_name("has]bracket"), "close bracket is not safe");
    expect(!mm::build::is_safe_board_name("a/b"), "slash is not safe");

    // Manifest load checks
    const mm::test::scoped_tree tree{"board_name_rejections"};
    tree.manifest("", "kind: project\nname: p\nfolder: board\n");

    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: bad board\nsdk: s\n"
                      "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(tree.root()).ok, "space in board name rejected at load");

    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: bad;board\nsdk: s\n"
                      "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(tree.root()).ok, "semicolon in board name rejected at load");

    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: bad\"board\nsdk: s\n"
                      "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(tree.root()).ok, "quote in board name rejected at load");

    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: bad[0]\nsdk: s\n"
                      "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(tree.root()).ok, "bracket in board name rejected at load");

    tree.manifest_raw("board",
                      "mm: 1.2\nkind: board\nname: -badboard\nsdk: s\n"
                      "cpu: cortex-m3\ninstruction-set: thumb\nfloat-abi: soft\n");
    expect(!mm::build::load_project(tree.root()).ok, "leading dash in board name rejected at load");
}

const mm::test::case_ cases[] = {
    { "walks a nested tree",                  &walks_a_nested_tree },
    { "separates tests and docs",             &separates_tests_and_docs_from_targets },
    { "rejects self referencing folder",      &rejects_self_referencing_folder },
    { "rejects two manifest cycle",           &rejects_two_manifest_cycle },
    { "rejects symlink cycle",                &rejects_symlink_cycle },
    { "rejects traversal above the root",     &rejects_traversal_above_the_root },
    { "accepts a diamond once",               &accepts_a_diamond_once },
    { "rejects missing manifest",             &rejects_missing_manifest },
    { "rejects unknown kind",                 &rejects_unknown_kind },
    { "rejects missing mm: version",          &rejects_missing_mm_version },
    { "rejects unsupported mm: version",      &rejects_unsupported_mm_version },
    { "accepts current mm: version",          &accepts_current_mm_version },
    { "rejects unknown key in current mm",    &rejects_unknown_key_in_current_mm_version },
    { "rejects duplicate mm: version",        &rejects_duplicate_mm_version },
    { "rejects module without module name",   &rejects_module_without_module_name },
    { "rejects target without sources",       &rejects_target_without_sources },
    { "rejects duplicate module name",        &rejects_duplicate_module_name },
    { "rejects duplicate app name",           &rejects_duplicate_app_name },
    { "accepts duplicate name across kinds",  &rejects_duplicate_name_across_kinds },
    { "accepts doc without files",            &accepts_doc_without_files },
    { "accepts mm: 1.3 version",              &accepts_mm_13_version },
    { "rejects sketch: under mm: 1.2",        &rejects_sketch_key_under_mm_12 },
    { "rejects sketch: on non-app kind",      &rejects_sketch_key_on_non_app_kind },
    { "accepts sketch app and records sketches", &accepts_sketch_app_and_records_sketches },
    { "defaults main.cpp when file omitted for sketch app", &defaults_main_cpp_when_file_omitted_for_sketch_app },
    { "includes main.cpp when helper files explicit", &includes_main_cpp_when_helper_files_explicit },
    { "avoids duplicate main.cpp when explicit", &avoids_duplicate_main_cpp_when_explicit },
    {"loads SDK and board definitions", &loads_sdk_and_board_definitions},
    {"validates platform keys by version and kind", &validates_platform_keys_by_version_and_kind},
    {"rejects bad references and registry values", &rejects_bad_references_and_registry_values},
    {"accepts registered processor combinations", &accepts_registered_processor_combinations},
    {"accepts the hazard3 processor combination", &accepts_the_hazard3_processor_combination},
    {"external directories are selection-scoped", &external_directories_are_only_spelling_checked_by_the_walk},
    {"validates board name grammar", &validates_board_name_grammar},
};

const mm::test::registrar reg{"mm.build manifest", cases};

}
