// Black box tests for mm.model, run against this project's own real tree
// (the test binary's working directory is the project root; see
// tools/test's front end). Prior to fixing a review finding, every node
// here returned nullptr/empty for parent()/children() and the root was a
// hard-coded "modules.cpp" ProjectNode regardless of what was actually
// loaded; these cases pin the real values instead.

#include <cstddef>
#include <string>
#include <string_view>

import mm.model;
import mm.test;
import models.manifest;
import models.platform;
import models.repository;
import models.tool;

namespace {

const models::ManifestNode* find_child(const models::ManifestNode& node, std::string_view name) {
    for (const auto* child : node.children())
        if (child->name() == name) return child;
    return nullptr;
}

void load_of_the_real_root_succeeds() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    mm::test::expect(ok, "expected loading this project's own root to succeed");
}

void root_reflects_the_real_project_manifest() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    mm::test::expect(root.name() == "modules.cpp",
                     "expected the root's name to come from the real mm.mdy, not a hard-coded value");
    mm::test::expect(root.kind() == models::Kind::Project, "expected the root to be Kind::Project");
    mm::test::expect(root.parent() == nullptr, "expected the root to have no parent");
}

void root_children_include_a_real_directory_node() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    const auto* apps = find_child(root, "apps");
    const auto* libraries = find_child(root, "libraries");
    mm::test::expect(apps != nullptr, "expected the root's children to include apps/");
    mm::test::expect(libraries != nullptr,
                     "expected the root's children to include libraries/");
    if (apps != nullptr)
        mm::test::expect(apps->kind() == models::Kind::Directory, "expected apps/ to be Kind::Directory");
}

void repository_exposes_platform_definitions() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto sdks = loaded.repository().sdks();
    const auto boards = loaded.repository().boards();
    mm::test::expect(ok && sdks.size() == 9,
                     "expected all nine SDK definitions from the manifest walk");
    mm::test::expect(boards.size() == 18,
                     "expected all eighteen board definitions from the manifest walk");

    // An SDL board binds one provider module to two interfaces. Two bindings
    // naming one module is the shape a board takes when a single provider
    // serves both roles, and nothing else in the tree exercises it. Both
    // architectures are checked, because a second one added by copying is
    // exactly where a binding goes missing unnoticed.
    for (const auto* name : {"sdl-linux-aarch64", "sdl-linux-x86_64"}) {
        const models::BoardNode* sdl_linux = nullptr;
        for (const auto* board : boards)
            if (board->name() == name) sdl_linux = board;
        mm::test::expect(sdl_linux != nullptr,
                         std::string("expected the ") + name + " board definition");
        if (sdl_linux == nullptr) continue;

        const auto bindings = sdl_linux->platform_providers();
        std::size_t named_sdl = 0;
        bool display_bound = false;
        bool touch_bound = false;
        for (const auto& binding : bindings) {
            if (binding.provider_module != "platform.linux.sdl") continue;
            ++named_sdl;
            if (binding.interface_module == "mm.display") display_bound = true;
            if (binding.interface_module == "mm.touch") touch_bound = true;
        }
        mm::test::expect(named_sdl == 2 && display_bound && touch_bound,
                         std::string("expected both ") + name +
                             " bindings to name one provider module");
        // The base board's map binding survives derivation untouched, which is
        // what says the derived board replaced two interfaces and not the set.
        mm::test::expect(bindings.size() == 3,
                         std::string("expected ") + name +
                             " to keep its inherited map binding");
    }
    const models::BoardNode* mps2 = nullptr;
    const models::BoardNode* rp2040 = nullptr;
    const models::BoardNode* rp2350 = nullptr;
    const models::BoardNode* pico = nullptr;
    const models::BoardNode* pico_w = nullptr;
    const models::BoardNode* pico2 = nullptr;
    const models::BoardNode* pico2_riscv = nullptr;
    const models::BoardNode* pico2_w_arm = nullptr;
    const models::BoardNode* pico2_w_riscv = nullptr;
    const models::BoardNode* widget = nullptr;
    const models::BoardNode* pico_epaper = nullptr;
    const models::BoardNode* pico_epaper_b = nullptr;
    for (const auto* b : boards) {
        if (b->name() == "mps2-an385") mps2 = b;
        if (b->name() == "rp2040-ram") rp2040 = b;
        if (b->name() == "rp2350-ram") rp2350 = b;
        if (b->name() == "pico") pico = b;
        if (b->name() == "pico-w") pico_w = b;
        if (b->name() == "pico2-arm") pico2 = b;
        if (b->name() == "pico2-riscv") pico2_riscv = b;
        if (b->name() == "pico2-w-arm") pico2_w_arm = b;
        if (b->name() == "pico2-w-riscv") pico2_w_riscv = b;
        if (b->name() == "widget-rp2040") widget = b;
        if (b->name() == "pico_epaper") pico_epaper = b;
        if (b->name() == "pico_epaper_b") pico_epaper_b = b;
    }
    mm::test::expect(mps2 != nullptr && mps2->kind() == models::Kind::Board &&
                         mps2->sdk() == "arm-none-eabi-newlib" &&
                         mps2->cpu() == "cortex-m3" &&
                         mps2->sources().size() == 1,
                     "expected mps2 board's SDK, processor, and source");
    mm::test::expect(rp2040 != nullptr && rp2040->kind() == models::Kind::Board &&
                         rp2040->sdk() == "arm-none-eabi-newlib" &&
                         rp2040->cpu() == "cortex-m0plus" &&
                         rp2040->sources().size() == 1,
                     "expected rp2040 board's SDK, processor, and source");
    mm::test::expect(rp2350 != nullptr && rp2350->kind() == models::Kind::Board &&
                         rp2350->sdk() == "arm-none-eabi-newlib" &&
                         rp2350->cpu() == "cortex-m33" &&
                         rp2350->sources().size() == 1,
                     "expected rp2350 board's SDK, processor, and source");
    mm::test::expect(pico != nullptr && pico->sdk() == "pico-arm" &&
                         pico->cpu() == "cortex-m0plus" && pico->sources().empty(),
                     "expected Pico SDK RP2040 board definition");
    mm::test::expect(pico_w != nullptr && pico_w->sdk() == "pico-arm" &&
                         pico_w->cpu() == "cortex-m0plus" && pico_w->sources().empty(),
                     "expected Pico W SDK RP2040 board definition");
    mm::test::expect(pico2 != nullptr && pico2->sdk() == "pico-arm" &&
                         pico2->cpu() == "cortex-m33" && pico2->sources().empty(),
                     "expected Pico SDK RP2350 Arm board definition");
    mm::test::expect(pico2_riscv != nullptr && pico2_riscv->sdk() == "pico-riscv" &&
                         pico2_riscv->cpu() == "hazard3" && pico2_riscv->sources().empty(),
                     "expected Pico SDK RP2350 RISC-V board definition");
    mm::test::expect(pico2_w_arm != nullptr && pico2_w_arm->sdk() == "pico-arm" &&
                         pico2_w_arm->cpu() == "cortex-m33" &&
                         pico2_w_arm->sources().empty(),
                     "expected Pico 2 W SDK RP2350 Arm board definition");
    mm::test::expect(pico2_w_riscv != nullptr &&
                         pico2_w_riscv->sdk() == "pico-riscv" &&
                         pico2_w_riscv->cpu() == "hazard3" &&
                         pico2_w_riscv->sources().empty(),
                     "expected Pico 2 W SDK RP2350 RISC-V board definition");
    mm::test::expect(widget != nullptr && widget->kind() == models::Kind::Board &&
                         widget->sdk() == "arm-none-eabi-newlib" &&
                         widget->cpu() == "cortex-m0plus" &&
                         widget->sources().size() == 1 &&
                         widget->linker_script() == "boards/widget-rp2040/board/link.ld",
                     "expected widget-rp2040 derived board definition");
    mm::test::expect(pico_epaper != nullptr &&
                         pico_epaper->kind() == models::Kind::Board &&
                         pico_epaper->sdk() == "pico-arm" &&
                         pico_epaper->cpu() == "cortex-m0plus" &&
                         pico_epaper->sources().empty() &&
                         pico_epaper->derives_from() == pico,
                     "expected pico_epaper to derive from the Pico ARM board");
    mm::test::expect(pico_epaper_b != nullptr &&
                         pico_epaper_b->kind() == models::Kind::Board &&
                         pico_epaper_b->sdk() == "pico-arm" &&
                         pico_epaper_b->cpu() == "cortex-m0plus" &&
                         pico_epaper_b->sources().empty() &&
                         pico_epaper_b->derives_from() == pico,
                     "expected pico_epaper_b to derive from the Pico ARM board");
}

void repository_exposes_provider_declarations() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto modules = loaded.repository().modules();
    const auto sdks = loaded.repository().sdks();
    const auto boards = loaded.repository().boards();

    const models::ModuleNode* interface = nullptr;
    const models::ModuleNode* stdio_interface = nullptr;
    for (const auto* module : modules) {
        if (module->exported_module_name() == "mm.mcu") interface = module;
        if (module->exported_module_name() == "mm.stdio") stdio_interface = module;
    }
    mm::test::expect(ok && interface != nullptr && interface->platform_interface(),
                     "expected mm.mcu to expose its platform-interface marker");
    mm::test::expect(stdio_interface != nullptr && stdio_interface->platform_interface(),
                     "expected mm.stdio to expose its platform-interface marker");

    const models::SdkNode* pico_arm = nullptr;
    for (const auto* sdk : sdks)
        if (sdk->name() == "pico-arm") pico_arm = sdk;
    mm::test::expect(pico_arm != nullptr, "expected the Pico ARM SDK definition");
    if (pico_arm != nullptr) {
        const auto bindings = pico_arm->platform_providers();
        mm::test::expect(bindings.size() == 2 &&
                             bindings[0].interface_module == "mm.mcu" &&
                             bindings[0].provider_module == "platform.pico.mcu" &&
                             bindings[1].interface_module == "mm.stdio" &&
                             bindings[1].provider_module == "platform.pico.stdio",
                         "expected the SDK's authored MCU and console bindings");
    }

    const models::BoardNode* rp2040 = nullptr;
    for (const auto* board : boards)
        if (board->name() == "rp2040-ram") rp2040 = board;
    mm::test::expect(rp2040 != nullptr,
                     "expected the direct-register RP2040 board definition");
    if (rp2040 != nullptr) {
        const auto bindings = rp2040->platform_providers();
        mm::test::expect(bindings.size() == 1 &&
                             bindings.front().interface_module == "mm.mcu" &&
                             bindings.front().provider_module ==
                                 "platform.rp2040_ram.mcu",
                         "expected the board's authored provider binding");
    }

    const models::BoardNode* widget_board = nullptr;
    const models::BoardNode* pico_epaper_board = nullptr;
    const models::BoardNode* pico_epaper_b_board = nullptr;
    for (const auto* board : boards)
        if (board->name() == "widget-rp2040") widget_board = board;
        else if (board->name() == "pico_epaper") pico_epaper_board = board;
        else if (board->name() == "pico_epaper_b") pico_epaper_b_board = board;
    mm::test::expect(widget_board != nullptr,
                     "expected widget-rp2040 board definition");
    if (widget_board != nullptr) {
        const auto bindings = widget_board->platform_providers();
        mm::test::expect(bindings.size() == 1 &&
                             bindings.front().interface_module == "mm.mcu" &&
                             bindings.front().provider_module ==
                                 "platform.widget_rp2040.mcu",
                         "expected widget-rp2040 board's provider binding");
    }

    mm::test::expect(pico_epaper_board != nullptr,
                     "expected pico_epaper board definition");
    if (pico_epaper_board != nullptr) {
        const auto bindings = pico_epaper_board->platform_providers();
        mm::test::expect(bindings.size() == 1 &&
                             bindings.front().interface_module == "mm.display" &&
                             bindings.front().provider_module ==
                                 "platform.pico_epaper.display",
                         "expected pico_epaper to bind its display provider");
    }

    const models::BoardNode* touch_lcd = nullptr;
    for (const auto* board : boards)
        if (board->name() == "rp2350_touch_lcd_28") touch_lcd = board;
    mm::test::expect(touch_lcd != nullptr, "expected the RP2350 touch LCD board");
    if (touch_lcd != nullptr) {
        const auto bindings = touch_lcd->platform_providers();
        mm::test::expect(bindings.size() == 4,
                         "expected a board to bind four interfaces at once");
        bool display = false;
        bool touch = false;
        bool imu = false;
        bool rtc = false;
        for (const auto& binding : bindings) {
            if (binding.interface_module == "mm.display") display = true;
            if (binding.interface_module == "mm.touch") touch = true;
            if (binding.interface_module == "mm.imu") imu = true;
            if (binding.interface_module == "mm.rtc") rtc = true;
        }
        mm::test::expect(display && touch && imu && rtc,
                         "expected each interface bound exactly once");
    }

    mm::test::expect(pico_epaper_b_board != nullptr,
                     "expected pico_epaper_b board definition");
    if (pico_epaper_b_board != nullptr) {
        const auto bindings = pico_epaper_b_board->platform_providers();
        mm::test::expect(bindings.size() == 1 &&
                             bindings.front().interface_module == "mm.display" &&
                             bindings.front().provider_module ==
                                 "platform.pico_epaper_b.display",
                         "expected pico_epaper_b to bind its display provider");
    }
}

void child_and_parent_agree_with_each_other() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    const auto* apps = find_child(root, "apps");
    mm::test::expect(apps != nullptr, "expected apps/ to be a child of the root");
    if (apps == nullptr) return;

    const auto* main_app = find_child(*apps, "main");
    mm::test::expect(main_app != nullptr, "expected apps/main to be a child of apps/");
    if (main_app == nullptr) return;

    mm::test::expect(main_app->parent() == apps,
                     "expected apps/main's parent() to be the same object as apps/'s child entry for it");
    mm::test::expect(main_app->kind() == models::Kind::App, "expected apps/main to be Kind::App");

    const auto* boards_dir = find_child(root, "boards");
    mm::test::expect(boards_dir != nullptr, "expected boards/ to be a child of the root");
    if (boards_dir != nullptr) {
        const auto* platform = find_child(*boards_dir, "widget-rp2040-platform");
        mm::test::expect(platform != nullptr,
                         "expected widget-rp2040-platform to be a child of boards/");
        if (platform != nullptr) {
            mm::test::expect(platform->parent() == boards_dir,
                             "expected widget-rp2040-platform's parent() to be boards/");
            const auto* board_node = find_child(*platform, "widget-rp2040");
            const auto* mcu_node = find_child(*platform, "widget-rp2040-mcu");
            mm::test::expect(board_node != nullptr,
                             "expected widget-rp2040 board child");
            mm::test::expect(mcu_node != nullptr,
                             "expected widget-rp2040-mcu module child");
            if (board_node != nullptr)
                mm::test::expect(board_node->parent() == platform,
                                 "expected board node's parent() to be platform");
            if (mcu_node != nullptr)
                mm::test::expect(mcu_node->parent() == platform,
                                 "expected mcu node's parent() to be platform");
        }
    }
}

void every_app_reaches_the_root_by_walking_parent() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    for (const auto* app : loaded.repository().apps()) {
        const models::ManifestNode* walk = app;
        std::size_t hops = 0;
        while (walk->parent() != nullptr && hops < 20) {
            walk = walk->parent();
            ++hops;
        }
        mm::test::expect(walk == &root,
                         "expected walking parent() from an app to terminate at the root");
        mm::test::expect(hops < 20, "expected the parent chain to end, not cycle");
    }
}

void load_of_a_nonexistent_directory_fails() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load("this/directory/does/not/exist", ok);
    mm::test::expect(!ok, "expected loading a nonexistent directory to fail");
}

// Not a count comparison: build0 has no declaring app, and build1 shares
// its declaring app with out/bin/build, so tools().size() is not
// apps().size() any more. The real invariant is one directional: every app
// has at least one tool that declares it, checked per app by identity.
void every_app_has_a_declared_tool() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto tools = loaded.tools();

    for (const auto* app : loaded.repository().apps()) {
        bool found = false;
        for (const auto* tool : tools)
            if (tool->declared_by() == app) found = true;
        mm::test::expect(found, "expected every app to have at least one declared tool");
    }
}

void build0_and_build1_are_tools_without_and_with_a_declared_app() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto tools = loaded.tools();

    const models::Tool* build0 = nullptr;
    const models::Tool* build1 = nullptr;
    for (const auto* tool : tools) {
        if (tool->name() == "build0") build0 = tool;
        if (tool->name() == "build1") build1 = tool;
    }

    mm::test::expect(build0 != nullptr, "expected a build0 tool");
    mm::test::expect(build1 != nullptr, "expected a build1 tool");
    if (build0 == nullptr || build1 == nullptr) return;

    mm::test::expect(build0->declared_by() == nullptr,
                     "expected build0 to have no declaring manifest");

    const models::AppNode* build_app = nullptr;
    for (const auto* app : loaded.repository().apps())
        if (app->name() == "build") build_app = app;

    mm::test::expect(build_app != nullptr, "expected a declared build app");
    mm::test::expect(build1->declared_by() == build_app,
                     "expected build1 to share its declaring app with out/bin/build");
}

void board_derivation_provenance_and_sequence_entries() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    mm::test::expect(ok, "expected load to succeed");
    const auto boards = loaded.repository().boards();

    const models::BoardNode* rp2040 = nullptr;
    const models::BoardNode* pico = nullptr;
    const models::BoardNode* pico2 = nullptr;
    const models::BoardNode* widget = nullptr;
    for (const auto* b : boards) {
        if (b->name() == "rp2040-ram") rp2040 = b;
        if (b->name() == "pico") pico = b;
        if (b->name() == "pico2-arm") pico2 = b;
        if (b->name() == "widget-rp2040") widget = b;
    }

    mm::test::expect(rp2040 != nullptr, "expected rp2040-ram board");
    if (rp2040 != nullptr) {
        mm::test::expect(rp2040->derives_from() == nullptr,
                         "expected rp2040-ram to derive from nothing");
        mm::test::expect(rp2040->sdk() == "arm-none-eabi-newlib",
                         "expected rp2040 SDK");
        mm::test::expect(
            rp2040->sdk_provenance().origin == models::ValueOrigin::Authored,
            "expected rp2040 SDK to be authored");
        mm::test::expect(
            rp2040->sdk_provenance().manifest ==
                "platforms/pico/rp2040-ram/mm.mdy",
            "expected rp2040 SDK manifest");
        mm::test::expect(
            rp2040->cpu_provenance().origin == models::ValueOrigin::Authored,
            "expected rp2040 CPU to be authored");
        mm::test::expect(
            rp2040->instruction_set_provenance().origin ==
                models::ValueOrigin::Authored,
            "expected rp2040 instruction-set to be authored");
        mm::test::expect(
            rp2040->float_abi_provenance().origin ==
                models::ValueOrigin::Authored,
            "expected rp2040 float-abi to be authored");
        mm::test::expect(
            rp2040->security_domain_provenance().origin ==
                models::ValueOrigin::SchemaDefault,
            "expected rp2040 security-domain to be schema default");
        mm::test::expect(
            rp2040->security_domain_provenance().manifest.empty(),
            "expected empty manifest for schema default");
        mm::test::expect(
            rp2040->machine_provenance().origin ==
                models::ValueOrigin::Authored,
            "expected rp2040 machine to be authored");
        mm::test::expect(
            rp2040->linker_script_provenance().origin ==
                models::ValueOrigin::Authored,
            "expected rp2040 linker-script to be authored");
        mm::test::expect(
            rp2040->sources().size() == 1 &&
                rp2040->source_entries().size() == 1,
            "expected 1 source entry for rp2040");
        if (!rp2040->source_entries().empty()) {
            mm::test::expect(
                rp2040->source_entries().front().manifest ==
                    "platforms/pico/rp2040-ram/mm.mdy",
                "expected rp2040 source supplying manifest");
        }
        mm::test::expect(
            rp2040->provides().size() == 3 &&
                rp2040->provides_entries().size() == 3,
            "expected 3 provides entries for rp2040");
        mm::test::expect(
            rp2040->platform_providers().size() == 1 &&
                rp2040->platform_provider_entries().size() == 1,
            "expected 1 platform-provider entry for rp2040");
        mm::test::expect(
            rp2040->declared_platform_providers().size() == 1 &&
                rp2040->declared_platform_provider_entries().size() == 1,
            "expected 1 declared platform-provider for rp2040");
    }

    mm::test::expect(pico != nullptr, "expected pico board");
    if (pico != nullptr) {
        mm::test::expect(
            pico->linker_script().empty() &&
                pico->linker_script_provenance().origin ==
                    models::ValueOrigin::Absent,
            "expected pico linker-script to be absent");
        mm::test::expect(
            pico->linker_script_provenance().manifest.empty(),
            "expected empty manifest for absent linker-script");
    }

    mm::test::expect(pico2 != nullptr, "expected pico2-arm board");
    if (pico2 != nullptr) {
        mm::test::expect(
            pico2->security_domain() == "secure" &&
                pico2->security_domain_provenance().origin ==
                    models::ValueOrigin::Authored,
            "expected pico2-arm security-domain to be authored");
    }

    mm::test::expect(widget != nullptr, "expected widget-rp2040 board");
    if (widget != nullptr) {
        mm::test::expect(widget->derives_from() == rp2040,
                         "expected widget-rp2040 to derive from rp2040-ram");
        if (widget->derives_from() != nullptr) {
            mm::test::expect(
                widget->derives_from()->derives_from() == nullptr,
                "expected widget-rp2040 base to have no base");
        }
        mm::test::expect(widget->sdk() == "arm-none-eabi-newlib",
                         "expected widget-rp2040 resolved SDK");
        mm::test::expect(
            widget->sdk_provenance().origin == models::ValueOrigin::Inherited,
            "expected widget-rp2040 SDK to be inherited");
        mm::test::expect(
            widget->sdk_provenance().manifest ==
                "platforms/pico/rp2040-ram/mm.mdy",
            "expected widget-rp2040 SDK inherited manifest");
        mm::test::expect(
            widget->cpu_provenance().origin == models::ValueOrigin::Inherited,
            "expected widget-rp2040 CPU to be inherited");
        mm::test::expect(
            widget->instruction_set_provenance().origin ==
                models::ValueOrigin::Inherited,
            "expected widget-rp2040 instruction-set to be inherited");
        mm::test::expect(
            widget->float_abi_provenance().origin ==
                models::ValueOrigin::Inherited,
            "expected widget-rp2040 float-abi to be inherited");
        mm::test::expect(
            widget->security_domain_provenance().origin ==
                models::ValueOrigin::SchemaDefault,
            "expected widget-rp2040 security-domain to be schema default");
        mm::test::expect(
            widget->machine() == "rp2040" &&
                widget->machine_provenance().origin ==
                    models::ValueOrigin::Inherited,
            "expected widget-rp2040 machine to be inherited");
        mm::test::expect(
            widget->machine_provenance().manifest ==
                "platforms/pico/rp2040-ram/mm.mdy",
            "expected widget-rp2040 machine inherited manifest");
        mm::test::expect(
            widget->linker_script() ==
                "boards/widget-rp2040/board/link.ld" &&
                widget->linker_script_provenance().origin ==
                    models::ValueOrigin::Authored,
            "expected widget-rp2040 linker-script to be authored locally");
        mm::test::expect(
            widget->linker_script_provenance().manifest ==
                "boards/widget-rp2040/board/mm.mdy",
            "expected widget-rp2040 linker-script manifest");
        mm::test::expect(
            widget->sources().size() == 1 &&
                widget->source_entries().size() == 1,
            "expected inherited source entry for widget-rp2040");
        if (widget->source_entries().size() == 1) {
            mm::test::expect(
                widget->source_entries()[0].manifest ==
                    "platforms/pico/rp2040-ram/mm.mdy",
                "expected base source manifest for first source");
        }
        mm::test::expect(
            widget->provides().size() == 3 &&
                widget->provides_entries().size() == 3,
            "expected 3 provides entries for widget-rp2040");
        mm::test::expect(
            widget->platform_providers().size() == 1 &&
                widget->platform_provider_entries().size() == 1,
            "expected 1 effective provider binding for widget-rp2040");
        if (!widget->platform_provider_entries().empty()) {
            mm::test::expect(
                widget->platform_provider_entries().front().manifest ==
                    "boards/widget-rp2040/board/mm.mdy",
                "expected widget-rp2040 overridden provider manifest");
        }
        mm::test::expect(
            widget->declared_platform_providers().size() == 1 &&
                widget->declared_platform_provider_entries().size() == 1,
            "expected 1 declared provider for widget-rp2040");
    }
}

const mm::test::case_ cases[] = {
    { "load of the real root succeeds",                &load_of_the_real_root_succeeds },
    { "root reflects the real project manifest",       &root_reflects_the_real_project_manifest },
    { "root children include a real directory node",   &root_children_include_a_real_directory_node },
    { "repository exposes platform definitions",       &repository_exposes_platform_definitions },
    { "repository exposes provider declarations",      &repository_exposes_provider_declarations },
    { "board derivation, provenance, and entries",      &board_derivation_provenance_and_sequence_entries },
    { "child and parent agree with each other",        &child_and_parent_agree_with_each_other },
    { "every app reaches the root by walking parent",  &every_app_reaches_the_root_by_walking_parent },
    { "load of a nonexistent directory fails",         &load_of_a_nonexistent_directory_fails },
    { "every app has a declared tool",                 &every_app_has_a_declared_tool },
    { "build0 and build1 declared_by()",               &build0_and_build1_are_tools_without_and_with_a_declared_app },
};

const mm::test::registrar reg{"mm.model structure", cases};

}  // namespace
