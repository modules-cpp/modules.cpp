// Black box tests for loading the compiler lane selected by out/config.mdy.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import mm.build;
import mm.configure;
import mm.test;

namespace {

using mm::test::expect;

void write(const std::filesystem::path& path, std::string_view front_matter) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    out << "---\n" << front_matter << "---\n";
}

constexpr std::string_view native_configuration =
    "mm: 1.0\n"
    "kind: configuration\n"
    "name: native\n"
    "build: release\n"
    "target-compiler: host\n"
    "host-compiler-family: clang\n"
    "host-compiler: clang++\n"
    "host-target: host\n"
    "host-platform: POSIX\n"
    "host-compile-flags: host compile flags\n"
    "host-link-flags: host link flags\n"
    "host-build-directory: out-host\n"
    "target-build-directory: out-host\n";

std::string platform_configuration() {
    return
        "mm: 2.0\n"
        "schema: configuration-2\n"
        "kind: configuration\n"
        "name: arm\n"
        "build: release\n"
        "target-compiler: cross\n"
        "target-host-capability: no\n"
        "host-compiler-family: gcc\n"
        "host-compiler: g++\n"
        "host-target: host\n"
        "host-platform: POSIX\n"
        "host-compile-flags: host compile flags\n"
        "host-link-flags: host link flags\n"
        "cross-compiler-family: gcc\n"
        "cross-compiler: arm-none-eabi-g++\n"
        "cross-target: arm-none-eabi\n"
        "cross-compile-flags: cross compile flags\n"
        "cross-link-flags: cross link flags\n"
        "cross-runner: qemu-system-arm\n"
        "cross-runner-prefix-argument: -M\n"
        "cross-runner-prefix-argument: mps2-an385\n"
        "cross-runner-image: option\n"
        "cross-runner-image-option: -kernel\n"
        "cross-runner-forwards-arguments: no\n"
        "cross-system: bare-metal\n"
        "cross-runtime: newlib\n"
        "cross-sdk: arm-none-eabi-newlib\n"
        "cross-sdk-manifest: platforms/sdk/mm.mdy\n"
        "cross-sdk-compiler-family: gcc\n"
        "cross-sdk-specs-argument: --specs=rdimon.specs\n"
        "cross-sdk-provides: runtime-init\n"
        "cross-sdk-provides: syscalls\n"
        "cross-board: mps2-an385\n"
        "cross-board-manifest: platforms/board/mm.mdy\n"
        "cross-board-machine: mps2-an385\n"
        "cross-board-linker-script: platforms/board/link.ld\n"
        "cross-board-source: platforms/board/vectors.cpp\n"
        "cross-board-provides: reset-vector\n"
        "cross-board-provides: initial-stack\n"
        "cross-board-provides: memory-layout\n"
        "cross-board-argument: -mcpu=cortex-m3\n"
        "cross-board-argument: -mthumb\n"
        "cross-board-argument: -mfloat-abi=soft\n"
        "host-build-directory: out-host\n"
        "target-build-directory: out-target-arm-none-eabi\n";
}

void platform_files(const mm::test::scoped_tree& tree) {
    write(tree.root() / "platforms/sdk/mm.mdy",
          "mm: 1.2\nkind: sdk\nname: arm-none-eabi-newlib\n");
    write(tree.root() / "platforms/board/mm.mdy",
          "mm: 1.2\nkind: board\nname: mps2-an385\n");
    std::ofstream(tree.root() / "platforms/board/link.ld") << "SECTIONS {}\n";
    std::ofstream(tree.root() / "platforms/board/vectors.cpp") << "int vector;\n";
}

void loads_the_host_selection() {
    const mm::test::scoped_tree tree{"build_native_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path, native_configuration);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, true, configuration),
                     "expected native configuration to load");
    const auto& toolchain = configuration.selected_toolchain();
    mm::test::expect(toolchain.compiler.invocation == "clang++",
                     "expected host compiler selection");
    mm::test::expect(toolchain.family == mm::build::CompilerFamily::Clang,
                     "expected Clang family selection");
    mm::test::expect(toolchain.compiler.arguments == "host compile flags" &&
                         toolchain.linker.arguments == "host link flags",
                     "expected host flags");
    mm::test::expect(toolchain.verbose,
                     "expected the caller's verbose setting to be retained");
    mm::test::expect(configuration.host_toolchain().target == "host" &&
                         configuration.cross_toolchain() == nullptr &&
                         !configuration.selects_cross() &&
                         !configuration.target_has_host_capability(),
                     "expected one retained host lane");
    mm::test::expect(configuration.build == mm::build::Build::Release,
                     "expected release build selection");
    mm::test::expect(configuration.build_directory == "out-host",
                     "expected target build directory");
}

void loads_the_cross_selection() {
    const mm::test::scoped_tree tree{"build_cross_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path,
          "mm: 1.0\n"
          "kind: configuration\n"
          "name: cross\n"
          "target-compiler: cross\n"
          "target-host-capability: yes\n"
          "host-compiler-family: clang\n"
          "host-compiler: clang++\n"
          "host-target: host\n"
          "host-platform: POSIX\n"
          "host-compile-flags: host compile flags\n"
          "host-link-flags: host link flags\n"
          "cross-compiler-family: gcc\n"
          "cross-compiler: aarch64-linux-gnu-g++\n"
          "cross-target: aarch64-linux-gnu\n"
          "cross-platform: POSIX\n"
          "cross-compile-flags: cross compile flags\n"
          "cross-link-flags: cross link flags\n"
          "cross-runner: qemu-m68k\n"
          "cross-runner-prefix-argument: -L\n"
          "cross-runner-prefix-argument: /sysroot\n"
          "cross-runner-image: positional\n"
          "cross-runner-forwards-arguments: yes\n"
          "cross-debugger: gdb-multiarch\n"
          "cross-debugger-prefix-argument: -q\n"
          "cross-debugger-connection: runner-remote\n"
          "cross-debugger-remote-endpoint: localhost:1234\n"
          "cross-debugger-runner-argument: -g\n"
          "cross-debugger-runner-argument: 1234\n"
          "host-build-directory: out-host\n"
          "target-build-directory: out/target/aarch64-linux-gnu\n");

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected cross configuration to load");
    const auto& toolchain = configuration.selected_toolchain();
    mm::test::expect(toolchain.compiler.invocation == "aarch64-linux-gnu-g++",
                     "expected cross compiler selection");
    mm::test::expect(toolchain.family == mm::build::CompilerFamily::Gcc,
                     "expected cross compiler family selection");
    mm::test::expect(toolchain.compiler.arguments == "cross compile flags" &&
                         toolchain.linker.arguments == "cross link flags",
                     "expected cross flags");
    mm::test::expect(toolchain.target == "aarch64-linux-gnu" &&
                         configuration.host_toolchain().compiler.invocation == "clang++" &&
                         configuration.cross_toolchain() != nullptr &&
                         configuration.selects_cross() &&
                         configuration.target_has_host_capability(),
                     "expected both host and selected cross lanes to be retained");
    mm::test::expect(toolchain.runner && toolchain.runner->invocation == "qemu-m68k" &&
                         toolchain.runner->prefix_arguments.size() == 2 &&
                         toolchain.runner->prefix_arguments[1] == "/sysroot" &&
                         toolchain.runner->forwards_arguments,
                     "expected ordered target runner settings");
    mm::test::expect(toolchain.debugger &&
                         toolchain.debugger->invocation == "gdb-multiarch" &&
                         toolchain.debugger->connection ==
                             mm::build::DebuggerConnection::RunnerRemote &&
                         toolchain.debugger->remote_endpoint == "localhost:1234" &&
                         toolchain.debugger->runner_arguments.size() == 2,
                     "expected target debugger settings");
    mm::test::expect(configuration.build_directory == "out/target/aarch64-linux-gnu",
                     "expected cross target build directory");
    mm::test::expect(configuration.build_directory_for(false) != nullptr &&
                         *configuration.build_directory_for(false) == "out-host" &&
                         configuration.build_directory_for(true) != nullptr &&
                         *configuration.build_directory_for(true) ==
                             "out/target/aarch64-linux-gnu",
                     "expected both lane directories to remain selectable");
}

void retains_an_unselected_cross_compiler() {
    const mm::test::scoped_tree tree{"build_unselected_cross_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path,
          "mm: 1.0\n"
          "kind: configuration\n"
          "name: unselected-cross\n"
          "target-compiler: host\n"
          "host-compiler-family: clang\n"
          "host-compiler: clang++\n"
          "host-target: host\n"
          "host-platform: POSIX\n"
          "host-compile-flags: host compile flags\n"
          "host-link-flags: host link flags\n"
          "cross-compiler-family: gcc\n"
          "cross-compiler: aarch64-linux-gnu-g++\n"
          "cross-target: aarch64-linux-gnu\n"
          "cross-platform: POSIX\n"
          "cross-compile-flags: cross compile flags\n"
          "cross-link-flags: cross link flags\n"
          "host-build-directory: out-host\n"
          "target-build-directory: out-host\n");

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, true, configuration),
                     "expected an unselected cross record to load");
    mm::test::expect(!configuration.selects_cross() &&
                         configuration.selected_toolchain().compiler.invocation == "clang++",
                     "expected the host lane to remain selected");
    mm::test::expect(configuration.cross_toolchain() != nullptr &&
                         configuration.cross_toolchain()->compiler.invocation ==
                             "aarch64-linux-gnu-g++" &&
                         configuration.cross_toolchain()->verbose,
                     "expected the unselected cross lane to be retained completely");
    mm::test::expect(configuration.build_directory == "out-host" &&
                         configuration.build_directory_for(true) != nullptr,
                     "expected host selection while retaining the cross directory");
}

void rejects_a_selected_but_missing_cross_compiler() {
    const mm::test::scoped_tree tree{"build_missing_cross_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto selection = text.find("target-compiler: host");
    text.replace(selection, std::string_view("target-compiler: host").size(),
                 "target-compiler: cross");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected a missing selected cross group to fail");
}

void rejects_an_escaping_build_directory() {
    const mm::test::scoped_tree tree{"build_escaping_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto directory = text.find("target-build-directory: out-host");
    text.replace(directory, std::string_view("target-build-directory: out-host").size(),
                 "target-build-directory: ../outside");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected an escaping target build directory to fail");
}

void resolves_one_project_configuration() {
    const mm::test::scoped_tree tree{"build_resolved_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path, native_configuration);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::resolve_configuration(tree.root(), false, configuration),
                     "expected project configuration to resolve");
    mm::test::expect(configuration.selected_toolchain().family ==
                             mm::build::CompilerFamily::Clang &&
                         configuration.selected_toolchain().compiler.invocation == "clang++",
                     "expected the resolved project compiler");
}

void resolves_the_shared_unconfigured_default() {
    const mm::test::scoped_tree tree{"build_default_configuration"};
    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::resolve_configuration(tree.root(), false, configuration),
                     "expected the default configuration to resolve");
    mm::test::expect(configuration.selected_toolchain().family ==
                             mm::build::CompilerFamily::Gcc &&
                         configuration.selected_toolchain().compiler.invocation == "g++" &&
                         configuration.build_directory == "out",
                     "expected the shared unconfigured GCC default");
}

void treats_an_older_configuration_as_gcc() {
    const mm::test::scoped_tree tree{"build_legacy_gcc_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto family = text.find("host-compiler-family: clang\n");
    text.erase(family, std::string_view("host-compiler-family: clang\n").size());
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected a configuration written before compiler families to load");
    mm::test::expect(configuration.selected_toolchain().family ==
                         mm::build::CompilerFamily::Gcc,
                     "expected a missing family to retain the historical GCC behavior");
}

void treats_an_older_configuration_as_debug() {
    const mm::test::scoped_tree tree{"build_legacy_debug_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto build = text.find("build: release\n");
    text.erase(build, std::string_view("build: release\n").size());
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected a configuration written before build selection to load");
    mm::test::expect(configuration.build == mm::build::Build::Debug,
                     "expected a missing build to retain debug behavior");
}

void rejects_an_unknown_build() {
    const mm::test::scoped_tree tree{"build_unknown_build"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto build = text.find("build: release");
    text.replace(build, std::string_view("build: release").size(), "build: optimized");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected an unknown build to fail");
}

void rejects_an_unknown_compiler_family() {
    const mm::test::scoped_tree tree{"build_unknown_compiler_family"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto family = text.find("host-compiler-family: clang");
    text.replace(family, std::string_view("host-compiler-family: clang").size(),
                 "host-compiler-family: msvc");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected an unknown compiler family to fail");
}

void rejects_a_runner_without_a_target() {
    const mm::test::scoped_tree tree{"build_runner_without_target"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    text += "cross-runner: qemu-m68k\n"
            "cross-runner-image: positional\n";
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected a runner without a target toolchain to fail");
}

void loads_a_strict_platform_configuration() {
    const mm::test::scoped_tree tree{"build_platform_configuration"};
    platform_files(tree);
    const auto path = tree.root() / "out/config.mdy";
    write(path, platform_configuration());

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected configuration-2 platform to load");
    const auto* platform = configuration.target_platform();
    mm::test::expect(configuration.configuration_2() && platform != nullptr &&
                         configuration.configured_target_platform() == platform,
                     "expected selected and configured target platform projections");
    if (platform == nullptr) return;
    mm::test::expect(platform->system == mm::configure::PlatformSystem::BareMetal &&
                         platform->runtime == mm::configure::PlatformRuntime::Newlib &&
                         platform->sdk && *platform->sdk == "arm-none-eabi-newlib" &&
                         platform->board && *platform->board == "mps2-an385" &&
                         platform->unresolved.empty(),
                     "expected complete bare-metal platform state");
    mm::test::expect(platform->board_sources.size() == 1 &&
                         platform->board_sources.front() == "platforms/board/vectors.cpp",
                     "expected recorded board source");
    const auto* toolchain = configuration.cross_toolchain();
    mm::test::expect(toolchain != nullptr &&
                         toolchain->compiler.arguments.find("-mcpu=cortex-m3") !=
                             std::string::npos &&
                         toolchain->linker.arguments.find("--specs=rdimon.specs") !=
                             std::string::npos &&
                         toolchain->linker.arguments.find("platforms/board/link.ld") !=
                             std::string::npos &&
                         toolchain->runner && !toolchain->runner->forwards_arguments,
                     "expected platform arguments and system runner on the target toolchain");
    const auto unit = mm::build::platform_unit(platform);
    mm::test::expect(unit && unit->kind == "board" && unit->sources.size() == 1,
                     "expected a buildable platform unit from the record");

    auto host_selected = platform_configuration();
    const auto selection = host_selected.find("target-compiler: cross");
    host_selected.replace(selection, std::string_view("target-compiler: cross").size(),
                          "target-compiler: host");
    write(path, host_selected);
    mm::test::expect(mm::build::load_configuration(path, false, configuration) &&
                         configuration.target_platform() == nullptr &&
                         configuration.configured_target_platform() != nullptr,
                     "expected platform presence not to override host selection");
}

void rejects_malformed_strict_platform_records() {
    const mm::test::scoped_tree tree{"build_platform_configuration_invalid"};
    platform_files(tree);
    const auto path = tree.root() / "out/config.mdy";
    const auto original = platform_configuration();

    for (const auto& mutation : {
             std::pair{std::string("unknown: value\n"), std::string("unknown key")},
             std::pair{std::string("cross-platform: POSIX\n"), std::string("legacy platform key")},
             std::pair{std::string("cross-runtime: newlib\n"), std::string("duplicated scalar")}}) {
        write(path, original + mutation.first);
        mm::build::BuildConfiguration configuration;
        mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                         "expected strict record to reject " + mutation.second);
    }

    auto missing = original;
    const auto runtime = missing.find("cross-runtime: newlib\n");
    missing.erase(runtime, std::string_view("cross-runtime: newlib\n").size());
    write(path, missing);
    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected strict record to reject a missing required platform key");
}

void legacy_platforms_are_total_and_permissive() {
    const mm::test::scoped_tree tree{"build_legacy_platform"};
    const auto path = tree.root() / "out/config.mdy";
    auto text = std::string(native_configuration);
    text +=
        "cross-compiler-family: gcc\n"
        "cross-compiler: unknown-g++\n"
        "cross-target: vendor-unknown-abi\n"
        "cross-platform: POSIX\n"
        "cross-compile-flags: unchanged compile flags\n"
        "cross-link-flags: unchanged link flags\n"
        "future-legacy-key: ignored\n";
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected legacy record to remain permissive");
    const auto* platform = configuration.configured_target_platform();
    mm::test::expect(platform != nullptr &&
                         platform->system == mm::configure::PlatformSystem::Unknown &&
                         platform->runtime == mm::configure::PlatformRuntime::Unknown &&
                         !platform->models_responsibilities,
                     "expected a total but unenforced legacy platform");
    mm::test::expect(configuration.host_platform().system ==
                         mm::configure::PlatformSystem::Posix,
                     "expected a total POSIX host platform");
}

void loads_c_compiler_from_configuration() {
    const mm::test::scoped_tree tree{"build_c_compiler_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    const std::string text =
        "mm: 1.0\n"
        "kind: configuration\n"
        "name: arm\n"
        "build: release\n"
        "target-compiler: cross\n"
        "host-compiler-family: gcc\n"
        "host-compiler: g++\n"
        "host-c-compiler: gcc\n"
        "host-target: host\n"
        "host-platform: POSIX\n"
        "host-compile-flags: host compile flags\n"
        "host-link-flags: host link flags\n"
        "cross-compiler-family: gcc\n"
        "cross-compiler: arm-none-eabi-g++\n"
        "cross-c-compiler: arm-none-eabi-gcc\n"
        "cross-target: arm-none-eabi\n"
        "cross-platform: POSIX\n"
        "cross-compile-flags: cross compile flags\n"
        "cross-link-flags: cross link flags\n"
        "host-build-directory: out-host\n"
        "target-build-directory: out-target-arm-none-eabi\n";
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected configuration with C compilers to load");
    mm::test::expect(configuration.host_toolchain().c_compiler.invocation == "gcc",
                     "expected host C compiler gcc");
    mm::test::expect(configuration.cross_toolchain() != nullptr &&
                     configuration.cross_toolchain()->c_compiler.invocation == "arm-none-eabi-gcc",
                     "expected cross C compiler arm-none-eabi-gcc");
    mm::test::expect(configuration.selected_toolchain().c_compiler.invocation == "arm-none-eabi-gcc",
                     "expected selected toolchain C compiler arm-none-eabi-gcc");
}

void loads_cross_link_external_configuration() {
    const mm::test::scoped_tree tree{"build_cross_link_external"};
    const auto sdk_path = tree.root() / "platforms/sdk/mm.mdy";
    write(sdk_path, "mm: 1.2\nkind: sdk\nname: cmake-demo\n");

    // 1. Hosted boardless lane with cross-link: external
    const auto m68k_path = tree.root() / "out/config-m68k.mdy";
    const std::string m68k_config =
        "mm: 2.0\n"
        "schema: configuration-2\n"
        "kind: configuration\n"
        "name: m68k-linux-external\n"
        "build: release\n"
        "target-compiler: cross\n"
        "target-host-capability: no\n"
        "host-compiler-family: gcc\n"
        "host-compiler: g++\n"
        "host-target: host\n"
        "host-platform: POSIX\n"
        "host-compile-flags: host compile flags\n"
        "host-link-flags: host link flags\n"
        "cross-compiler-family: gcc\n"
        "cross-compiler: m68k-linux-gnu-g++\n"
        "cross-c-compiler: m68k-linux-gnu-gcc\n"
        "cross-target: m68k-linux-gnu\n"
        "cross-compile-flags: cross compile flags\n"
        "cross-link-flags: cross link flags\n"
        "cross-system: linux\n"
        "cross-runtime: none\n"
        "cross-sdk: cmake-demo\n"
        "cross-sdk-manifest: platforms/sdk/mm.mdy\n"
        "cross-sdk-compiler-family: gcc\n"
        "cross-link: external\n"
        "host-build-directory: out-host\n"
        "target-build-directory: out-target-m68k-linux-gnu\n";
    write(m68k_path, m68k_config);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(m68k_path, false, configuration),
                     "expected hosted boardless external configuration to load");
    const auto* platform = configuration.configured_target_platform();
    mm::test::expect(platform != nullptr, "platform resolved");
    if (platform != nullptr) {
        mm::test::expect(platform->link_ownership == mm::configure::LinkOwnership::External,
                         "expected LinkOwnership::External");
        mm::test::expect(!platform->board.has_value(), "expected boardless lane");
    }

    // 2. Bare-metal board with cross-link: external (omits linker script and board sources)
    const auto board_manifest = tree.root() / "platforms/board/mm.mdy";
    write(board_manifest, "mm: 1.2\nkind: board\nname: pico\n");
    const auto arm_sdk_manifest = tree.root() / "platforms/pico-sdk/mm.mdy";
    write(arm_sdk_manifest, "mm: 1.2\nkind: sdk\nname: pico-arm\n");

    const auto bm_path = tree.root() / "out/config-bm.mdy";
    const std::string bm_config =
        "mm: 2.0\n"
        "schema: configuration-2\n"
        "kind: configuration\n"
        "name: pico-arm\n"
        "build: release\n"
        "target-compiler: cross\n"
        "target-host-capability: no\n"
        "host-compiler-family: gcc\n"
        "host-compiler: g++\n"
        "host-target: host\n"
        "host-platform: POSIX\n"
        "host-compile-flags: host compile flags\n"
        "host-link-flags: host link flags\n"
        "cross-compiler-family: gcc\n"
        "cross-compiler: arm-none-eabi-g++\n"
        "cross-c-compiler: arm-none-eabi-gcc\n"
        "cross-target: arm-none-eabi\n"
        "cross-compile-flags: cross compile flags\n"
        "cross-link-flags: cross link flags\n"
        "cross-system: bare-metal\n"
        "cross-runtime: none\n"
        "cross-sdk: pico-arm\n"
        "cross-sdk-manifest: platforms/pico-sdk/mm.mdy\n"
        "cross-sdk-compiler-family: gcc\n"
        "cross-sdk-provides: reset-vector\n"
        "cross-sdk-provides: initial-stack\n"
        "cross-sdk-provides: memory-layout\n"
        "cross-sdk-provides: runtime-init\n"
        "cross-sdk-provides: syscalls\n"
        "cross-link: external\n"
        "cross-board: pico\n"
        "cross-board-manifest: platforms/board/mm.mdy\n"
        "cross-board-machine: rp2040\n"
        "cross-board-argument: -mcpu=cortex-m0plus\n"
        "cross-board-argument: -mthumb\n"
        "cross-board-argument: -mfloat-abi=soft\n"
        "host-build-directory: out-host\n"
        "target-build-directory: out-target-arm-none-eabi\n";
    write(bm_path, bm_config);

    mm::build::BuildConfiguration bm_configuration;
    mm::test::expect(mm::build::load_configuration(bm_path, false, bm_configuration),
                     "expected bare-metal external configuration with board to load");
    const auto* bm_platform = bm_configuration.configured_target_platform();
    mm::test::expect(bm_platform != nullptr, "bm_platform resolved");
    if (bm_platform != nullptr) {
        mm::test::expect(bm_platform->link_ownership == mm::configure::LinkOwnership::External,
                         "expected LinkOwnership::External for bare metal");
        mm::test::expect(bm_platform->board && *bm_platform->board == "pico", "expected board pico");
        mm::test::expect(bm_platform->linker_script.empty(), "expected empty linker script");
        mm::test::expect(bm_platform->board_sources.empty(), "expected empty board sources");
    }

    // 3. Rejects cross-link: external carrying a linker script
    const auto with_ld_path = tree.root() / "out/config-with-ld.mdy";
    std::ofstream(tree.root() / "platforms/board/link.ld") << "SECTIONS {}\n";
    auto with_ld = bm_config;
    with_ld += "cross-board-linker-script: platforms/board/link.ld\n";
    write(with_ld_path, with_ld);
    mm::build::BuildConfiguration bad_config;
    mm::test::expect(!mm::build::load_configuration(with_ld_path, false, bad_config),
                     "expected cross-link: external with linker script to be rejected");

    // 4. Rejects omitting linker script without cross-link: external
    const auto without_ext_path = tree.root() / "out/config-no-ext.mdy";
    auto without_ext = bm_config;
    const auto cross_link_pos = without_ext.find("cross-link: external\n");
    without_ext.erase(cross_link_pos, std::string_view("cross-link: external\n").size());
    write(without_ext_path, without_ext);
    mm::test::expect(!mm::build::load_configuration(without_ext_path, false, bad_config),
                     "expected board omitting linker script without cross-link to be rejected");

    // 5. Rejects invalid cross-link value
    const auto bad_link_val_path = tree.root() / "out/config-bad-link.mdy";
    auto bad_link_val = bm_config;
    const auto ext_val_pos = bad_link_val.find("cross-link: external");
    bad_link_val.replace(ext_val_pos, std::string_view("cross-link: external").size(),
                         "cross-link: internal");
    write(bad_link_val_path, bad_link_val);
    mm::test::expect(!mm::build::load_configuration(bad_link_val_path, false, bad_config),
                     "expected invalid cross-link value to be rejected");

    // 6. Accepts cross-board-derives-from repeated and carries board sources in external link
    const auto derives_path = tree.root() / "out/config-derives.mdy";
    std::ofstream(tree.root() / "platforms/board/pins.cpp") << "int pin;\n";
    auto with_derives = bm_config;
    with_derives += "cross-board-derives-from: pico-base\n";
    with_derives += "cross-board-derives-from: vendor-base\n";
    with_derives += "cross-board-source: platforms/board/pins.cpp\n";
    write(derives_path, with_derives);
    mm::build::BuildConfiguration derives_config;
    mm::test::expect(mm::build::load_configuration(derives_path, false, derives_config),
                     "expected configuration with cross-board-derives-from and sources to load");
    const auto* derives_platform = derives_config.configured_target_platform();
    mm::test::expect(derives_platform != nullptr, "derives_platform resolved");
    if (derives_platform != nullptr) {
        const std::vector<std::string> expected_chain = {"pico-base", "vendor-base"};
        mm::test::expect(derives_platform->board_derives_from == expected_chain,
                         "expected repeated cross-board-derives-from loaded in order");
        const std::vector<std::filesystem::path> expected_sources = {
            "platforms/board/pins.cpp"
        };
        mm::test::expect(derives_platform->board_sources == expected_sources,
                         "expected board sources carried in external link");
    }

    // 7. Rejects cross-board-derives-from without cross-board
    const auto no_board_derives_path = tree.root() / "out/config-no-board-derives.mdy";
    auto no_board_derives = bm_config;
    const auto board_key_pos = no_board_derives.find("cross-board: pico\n");
    no_board_derives.erase(board_key_pos, std::string_view("cross-board: pico\n").size());
    const auto board_mf_pos = no_board_derives.find("cross-board-manifest: platforms/board/mm.mdy\n");
    no_board_derives.erase(board_mf_pos, std::string_view("cross-board-manifest: platforms/board/mm.mdy\n").size());
    const auto board_mach_pos = no_board_derives.find("cross-board-machine: rp2040\n");
    no_board_derives.erase(board_mach_pos, std::string_view("cross-board-machine: rp2040\n").size());
    const auto board_arg_pos = no_board_derives.find("cross-board-argument: -mcpu=cortex-m0plus\n");
    no_board_derives.erase(board_arg_pos, std::string_view("cross-board-argument: -mcpu=cortex-m0plus\n"
                                                           "cross-board-argument: -mthumb\n"
                                                           "cross-board-argument: -mfloat-abi=soft\n").size());
    no_board_derives += "cross-board-derives-from: pico\n";
    write(no_board_derives_path, no_board_derives);
    mm::test::expect(!mm::build::load_configuration(no_board_derives_path, false, bad_config),
                     "expected cross-board-derives-from without cross-board to be rejected");

    // 8. Rejects empty cross-board-derives-from
    const auto empty_derives_path = tree.root() / "out/config-empty-derives.mdy";
    auto empty_derives = bm_config;
    empty_derives += "cross-board-derives-from: \n";
    write(empty_derives_path, empty_derives);
    mm::test::expect(!mm::build::load_configuration(empty_derives_path, false, bad_config),
                     "expected empty cross-board-derives-from to be rejected");

    // 9. Rejects unsafe cross-board-derives-from name
    const auto unsafe_derives_path = tree.root() / "out/config-unsafe-derives.mdy";
    auto unsafe_derives = bm_config;
    unsafe_derives += "cross-board-derives-from: bad;name\n";
    write(unsafe_derives_path, unsafe_derives);
    mm::test::expect(!mm::build::load_configuration(unsafe_derives_path, false, bad_config),
                     "expected unsafe cross-board-derives-from to be rejected");
}

void detects_stale_configuration_record() {
    const mm::test::scoped_tree tree{"build_stale_config"};
    const auto sdk_manifest = tree.root() / "platforms/pico-sdk/mm.mdy";
    write(sdk_manifest, "mm: 1.2\nkind: sdk\nname: pico-arm\n");
    const auto board_manifest = tree.root() / "platforms/board/mm.mdy";
    write(board_manifest, "mm: 1.2\nkind: board\nname: pico\n");
    const auto bm_path = tree.root() / "out/config.mdy";
    const std::string bm_config =
        "mm: 2.0\n"
        "schema: configuration-2\n"
        "kind: configuration\n"
        "name: pico-arm\n"
        "build: release\n"
        "target-compiler: cross\n"
        "target-host-capability: no\n"
        "host-compiler-family: gcc\n"
        "host-compiler: g++\n"
        "host-target: host\n"
        "host-platform: POSIX\n"
        "host-compile-flags: flags\n"
        "host-link-flags: flags\n"
        "cross-compiler-family: gcc\n"
        "cross-compiler: arm-none-eabi-g++\n"
        "cross-target: arm-none-eabi\n"
        "cross-compile-flags: flags\n"
        "cross-link-flags: flags\n"
        "cross-system: bare-metal\n"
        "cross-runtime: none\n"
        "cross-sdk: pico-arm\n"
        "cross-sdk-manifest: platforms/pico-sdk/mm.mdy\n"
        "cross-sdk-compiler-family: gcc\n"
        "cross-sdk-provides: reset-vector\n"
        "cross-sdk-provides: initial-stack\n"
        "cross-sdk-provides: memory-layout\n"
        "cross-sdk-provides: runtime-init\n"
        "cross-sdk-provides: syscalls\n"
        "cross-link: external\n"
        "cross-board: pico\n"
        "cross-board-manifest: platforms/board/mm.mdy\n"
        "cross-board-machine: rp2040\n"
        "cross-board-argument: -mcpu=cortex-m0plus\n"
        "cross-board-argument: -mthumb\n"
        "cross-board-argument: -mfloat-abi=soft\n"
        "host-build-directory: out-host\n"
        "target-build-directory: out-target-arm-none-eabi\n";
    write(bm_path, bm_config);

    mm::build::BuildConfiguration config;
    mm::test::expect(mm::build::load_configuration(bm_path, false, config),
                     "config loads");

    mm::build::Project project;
    mm::build::SdkDefinition sdk;
    sdk.name = "pico-arm";
    sdk.library = "pico-sdk";
    project.sdks.push_back(sdk);

    mm::build::LibraryDefinition lib;
    lib.name = "pico-sdk";
    lib.external_build = "cmake";
    project.libraries.push_back(lib);

    mm::build::BoardDefinition board;
    board.name = "pico";
    board.chain = {"pico"};
    project.boards.push_back(board);

    // Both external and board matches -> OK
    mm::test::expect(mm::build::check_configuration_staleness(config, project, true, "build"),
                     "matching external-build and cross-link: external is not stale");

    // Host lane -> always OK
    mm::test::expect(mm::build::check_configuration_staleness(config, project, false, "build"),
                     "host lane ignores target link ownership");

    // Library changed: removed external-build -> stale!
    project.libraries.front().external_build.clear();
    mm::test::expect(!mm::build::check_configuration_staleness(config, project, true, "build"),
                     "removing external-build makes record stale");

    // Library has external-build, but record does not have cross-link: external -> stale!
    project.libraries.front().external_build = "cmake";
    const auto proj_link_path = tree.root() / "out/config-proj.mdy";
    std::ofstream(tree.root() / "platforms/board/link.ld") << "SECTIONS {}\n";
    std::ofstream(tree.root() / "platforms/board/vectors.cpp") << "int v;\n";
    auto proj_config = bm_config;
    const auto link_pos = proj_config.find("cross-link: external\n");
    proj_config.erase(link_pos, std::string_view("cross-link: external\n").size());
    proj_config += "cross-board-linker-script: platforms/board/link.ld\n";
    proj_config += "cross-board-source: platforms/board/vectors.cpp\n";
    write(proj_link_path, proj_config);
    mm::build::BuildConfiguration config_proj;
    mm::test::expect(mm::build::load_configuration(proj_link_path, false, config_proj),
                     "config without cross-link loads");
    mm::test::expect(!mm::build::check_configuration_staleness(config_proj, project, true, "build"),
                     "library declaring external-build against project record is stale");

    // Vanished SDK -> stale!
    auto vanished_sdk_project = project;
    vanished_sdk_project.sdks.clear();
    mm::test::expect(!mm::build::check_configuration_staleness(config, vanished_sdk_project, true, "build"),
                     "vanished SDK makes record stale");

    // SDK names no external library, but record is external -> stale!
    auto no_lib_sdk_project = project;
    no_lib_sdk_project.sdks.front().library.clear();
    mm::test::expect(!mm::build::check_configuration_staleness(config, no_lib_sdk_project, true, "build"),
                     "SDK naming no external library against external record is stale");

    // SDK library vanished -> stale!
    auto vanished_lib_project = project;
    vanished_lib_project.libraries.clear();
    mm::test::expect(!mm::build::check_configuration_staleness(config, vanished_lib_project, true, "build"),
                     "vanished SDK library makes record stale");

    // Vanished board -> stale!
    auto vanished_board_project = project;
    vanished_board_project.boards.clear();
    mm::test::expect(!mm::build::check_configuration_staleness(config, vanished_board_project, true, "build"),
                     "vanished board makes record stale");

    // Board chain changed -> stale!
    auto changed_chain_project = project;
    changed_chain_project.boards.front().chain = {"pico", "other-base"};
    mm::test::expect(!mm::build::check_configuration_staleness(config, changed_chain_project, true, "build"),
                     "board chain change makes record stale");

    // Derived board record matches live project
    const auto derived_config_path = tree.root() / "out/config-derived.mdy";
    auto derived_config_str = bm_config;
    const auto board_pos = derived_config_str.find("cross-board: pico\n");
    derived_config_str.replace(board_pos, std::string_view("cross-board: pico\n").size(),
                               "cross-board: widget-pico\ncross-board-derives-from: pico\n");
    write(derived_config_path, derived_config_str);
    mm::build::BuildConfiguration config_derived;
    mm::test::expect(mm::build::load_configuration(derived_config_path, false, config_derived),
                     "derived board config loads");
    auto derived_project = project;
    derived_project.boards.clear();
    mm::build::BoardDefinition widget_board;
    widget_board.name = "widget-pico";
    widget_board.chain = {"widget-pico", "pico"};
    derived_project.boards.push_back(widget_board);
    mm::test::expect(mm::build::check_configuration_staleness(config_derived, derived_project, true, "build"),
                     "matching derived board chain is not stale");

    // Derived board chain changed (live base changed) -> stale!
    derived_project.boards.front().chain = {"widget-pico", "pico2"};
    mm::test::expect(!mm::build::check_configuration_staleness(config_derived, derived_project, true, "build"),
                     "changed derived board chain makes record stale");
}

void loads_secure_cortex_m33_processor_snapshots() {
    const mm::test::scoped_tree tree{"build_secure_cortex_m33_configuration"};
    platform_files(tree);

    auto secure = platform_configuration();
    const auto cpu = secure.find("cross-board-argument: -mcpu=cortex-m3\n");
    secure.replace(cpu, std::string_view("cross-board-argument: -mcpu=cortex-m3\n").size(),
                   "cross-board-argument: -mcpu=cortex-m33\n");
    const auto float_abi = secure.find("cross-board-argument: -mfloat-abi=soft\n");
    secure.replace(float_abi,
                   std::string_view("cross-board-argument: -mfloat-abi=soft\n").size(),
                   "cross-board-argument: -mfloat-abi=softfp\n"
                   "cross-board-argument: -mcmse\n");

    const auto current_path = tree.root() / "out/current.mdy";
    write(current_path, secure);
    mm::build::BuildConfiguration current;
    mm::test::expect(mm::build::load_configuration(current_path, false, current),
                     "current Cortex-M33 Arm Secure processor snapshot loads");

    auto legacy = secure;
    const auto cmse = legacy.find("cross-board-argument: -mcmse\n");
    legacy.erase(cmse, std::string_view("cross-board-argument: -mcmse\n").size());
    const auto legacy_path = tree.root() / "out/legacy.mdy";
    write(legacy_path, legacy);
    mm::build::BuildConfiguration old;
    mm::test::expect(mm::build::load_configuration(legacy_path, false, old),
                     "pre-CMSE Cortex-M33 processor snapshot remains readable");
}


void hosted_lanes_declare_no_processor_arguments() {
    const auto check = [](mm::build::CompilerFamily family, std::string_view target,
                          std::string_view cpu) {
        const auto* entry = mm::build::find_processor_entry(
            family, target, cpu, "native", "native", "");
        expect(entry != nullptr, "hosted combination is registered");
        expect(mm::build::processor_argument_count(*entry) == 0,
               "a native lane adds no processor arguments");
    };
    for (const auto family : {mm::build::CompilerFamily::Gcc, mm::build::CompilerFamily::Clang}) {
        check(family, "aarch64-linux-gnu", "aarch64");
        check(family, "x86_64-linux-gnu", "x86_64");
    }
}

void bare_metal_rows_carry_their_cpu_arguments() {
    const auto* m3 = mm::build::find_processor_entry(
        mm::build::CompilerFamily::Gcc, "arm-none-eabi", "cortex-m3",
        "thumb", "soft", "non-secure");
    expect(m3 != nullptr && mm::build::processor_argument_count(*m3) == 3,
           "cortex-m3 carries cpu, thumb and float arguments");
    const auto* plain_m33 = mm::build::find_processor_entry(
        mm::build::CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33",
        "thumb", "softfp", "non-secure");
    const auto* secure_m33 = mm::build::find_processor_entry(
        mm::build::CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33",
        "thumb", "softfp", "secure");
    expect(plain_m33 != nullptr && secure_m33 != nullptr,
           "both cortex-m33 domains are registered");
    expect(secure_m33->arguments[3] == "-mcmse" &&
               mm::build::processor_argument_count(*secure_m33) == 4,
           "the secure domain appends the cmse argument");
    const auto* hazard3 = mm::build::find_processor_entry(
        mm::build::CompilerFamily::Gcc, "riscv32-pico-elf", "hazard3",
        "rv32imacb_zicsr_zifencei_zmmul_zaamo_zalrsc_zca_zcb_zcmp_zba_zbb_zbkb_zbs_xh3bextm",
        "soft", "non-secure");
    expect(hazard3 != nullptr && hazard3->arguments[0] == "-mcpu=hazard3-rp2350",
           "hazard3 keeps the measured pico argument");
}

void processor_lookup_is_keyed_on_family_and_security_domain() {
    const auto* secure = mm::build::find_processor_entry(
        mm::build::CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33",
        "thumb", "softfp", "secure");
    expect(secure != nullptr, "the secure row is found by its full key");
    expect(mm::build::find_processor_entry(
               mm::build::CompilerFamily::Clang, "arm-none-eabi", "cortex-m33",
               "thumb", "softfp", "secure") == nullptr,
           "the table is keyed on the compiler that will be invoked");
    const auto* defaulted = mm::build::find_processor_entry(
        mm::build::CompilerFamily::Gcc, "arm-none-eabi", "cortex-m33",
        "thumb", "softfp", "");
    expect(defaulted != nullptr && defaulted->security_domain == "non-secure",
           "an empty security domain defaults to non-secure");
}

void processor_lookup_by_arguments_matches_table_rows() {
    const auto* m3 = mm::build::find_processor_entry_by_arguments(
        mm::build::CompilerFamily::Gcc, "arm-none-eabi",
        {"-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft"});
    expect(m3 != nullptr && m3->cpu == "cortex-m3",
           "the argument list identifies its table row");
    expect(mm::build::find_processor_entry_by_arguments(
               mm::build::CompilerFamily::Gcc, "arm-none-eabi",
               {"-mcpu=cortex-m3"}) == nullptr,
           "a truncated argument list matches no row");
    expect(mm::build::find_processor_entry_by_arguments(
               mm::build::CompilerFamily::Clang, "arm-none-eabi",
               {"-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft"}) == nullptr,
           "arguments never cross a family boundary");
}

void compiler_family_name_spells_the_invoked_compiler() {
    expect(std::string(mm::build::compiler_family_name(mm::build::CompilerFamily::Gcc)) == "gcc",
           "gcc spells gcc");
    expect(std::string(mm::build::compiler_family_name(mm::build::CompilerFamily::Clang)) == "clang",
           "clang spells clang");
}

void build_name_spells_the_selected_build() {
    expect(std::string(mm::build::build_name(mm::build::Build::Debug)) == "debug",
           "debug spells debug");
    expect(std::string(mm::build::build_name(mm::build::Build::Release)) == "release",
           "release spells release");
}

const mm::test::case_ cases[] = {
    {"loads the host selection", &loads_the_host_selection},
    {"loads the cross selection", &loads_the_cross_selection},
    {"retains an unselected cross compiler", &retains_an_unselected_cross_compiler},
    {"rejects selected missing cross compiler", &rejects_a_selected_but_missing_cross_compiler},
    {"rejects escaping build directory", &rejects_an_escaping_build_directory},
    {"resolves one project configuration", &resolves_one_project_configuration},
    {"resolves shared unconfigured default", &resolves_the_shared_unconfigured_default},
    {"older configuration defaults to GCC", &treats_an_older_configuration_as_gcc},
    {"older configuration defaults to debug", &treats_an_older_configuration_as_debug},
    {"rejects unknown build", &rejects_an_unknown_build},
    {"rejects unknown compiler family", &rejects_an_unknown_compiler_family},
    {"rejects runner without target", &rejects_a_runner_without_a_target},
    {"loads strict platform configuration", &loads_a_strict_platform_configuration},
    {"rejects malformed strict platform records", &rejects_malformed_strict_platform_records},
    {"legacy platforms are total and permissive", &legacy_platforms_are_total_and_permissive},
    {"loads C compiler from configuration", &loads_c_compiler_from_configuration},
    {"loads cross-link external configuration", &loads_cross_link_external_configuration},
    {"detects stale configuration record", &detects_stale_configuration_record},
    {"loads secure Cortex-M33 processor snapshots", &loads_secure_cortex_m33_processor_snapshots},
    {"hosted lanes declare no processor arguments", &hosted_lanes_declare_no_processor_arguments},
    {"bare-metal rows carry their cpu arguments", &bare_metal_rows_carry_their_cpu_arguments},
    {"processor lookup is keyed on family and security domain", &processor_lookup_is_keyed_on_family_and_security_domain},
    {"processor lookup by arguments matches table rows", &processor_lookup_by_arguments_matches_table_rows},
    {"compiler family name spells the invoked compiler", &compiler_family_name_spells_the_invoked_compiler},
    {"build name spells the selected build", &build_name_spells_the_selected_build},
};

const mm::test::registrar reg{"mm.build config", cases};

}  // namespace
