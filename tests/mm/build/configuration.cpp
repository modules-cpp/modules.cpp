// Black box tests for loading the compiler lane selected by out/config.mdy.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

import mm.build;
import mm.configure;
import mm.test;

namespace {

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
};

const mm::test::registrar reg{"mm.build configuration", cases};

}  // namespace
