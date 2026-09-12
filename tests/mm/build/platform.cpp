// Manifest-side platform definition and version-gate tests.
#include <filesystem>
#include <fstream>

import mm.build;
import mm.test;

namespace {

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
                      "mm: 1.2\nkind: board\nname: old\nsdk: sdk\n"
                      "cpu: cortex-m33\ninstruction-set: thumb\nfloat-abi: softfp\n"
                      "security-domain: secure\n");
    expect(!mm::build::load_project(tree.root()).ok,
           "security-domain requires manifest version 1.4");
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
                            "mm: 1.4\nkind: board\nname: pico2\n"
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
                            "mm: 1.4\nkind: board\nname: bad\n"
                            "sdk: arm-none-eabi-newlib\ncpu: cortex-m33\n"
                            "instruction-set: thumb\nfloat-abi: softfp\n"
                            "security-domain: privileged\nlinker-script: link.ld\n"
                            "file: vectors.cpp\n");
    expect(!mm::build::load_project(secure_m33.root()).ok,
           "an unknown security domain is rejected");
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
    {"external directories are selection-scoped", &external_directories_are_only_spelling_checked_by_the_walk},
};

const mm::test::registrar reg{"mm.build platform", cases};

}
