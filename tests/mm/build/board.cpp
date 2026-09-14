// Manifest-side board derivation, grammar, and resolution tests.
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

void make_base_tree(const mm::test::scoped_tree& tree) {
    tree.manifest("", "kind: project\nname: p\nfolder: platforms\nfolder: boards\n");
    tree.manifest("platforms", "kind: dir\nname: platforms\nfolder: sdk\nfolder: board\n");
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
    tree.manifest("boards", "kind: dir\nname: boards\n");
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
    {"validates board name grammar", &validates_board_name_grammar},
    {"derives-from resolves platform identity", &derives_from_resolves_platform_identity},
    {"path resolution precedes merging fixture", &path_resolution_precedes_merging_fixture},
    {"scalar overrides and merges", &scalar_overrides_and_merges},
    {"platform provider precedence", &platform_provider_precedence},
    {"multi-level board chain", &multi_level_board_chain},
    {"rejects invalid derivations", &rejects_invalid_derivations},
    {"rejects linker script on externally linked board", &rejects_linker_script_on_externally_linked_board},
    {"diagnostics name supplying manifest", &diagnostics_name_supplying_manifest},
    {"requires-board exact matching", &requires_board_exact_matching},
};

const mm::test::registrar reg{"mm.build board", cases};

}
