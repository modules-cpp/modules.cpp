// Black box tests for mm.model::configuration(), in both cases it must
// describe: a tree with no out/config.mdy, and one configure has written.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

import mm.configure;
import mm.model;
import mm.test;
import models.configuration;
import models.platform;
import models.tool;
import models.toolchain;

namespace {

using mm::test::expect;

mm::configure::Settings host_settings() {
    mm::configure::Settings settings;
    settings.name = "gcc-release";
    settings.build = mm::configure::Build::Release;
    settings.host = {
        mm::configure::CompilerFamily::Gcc, "g++-15", "host", "POSIX",
        std::string(mm::configure::build_compile_flags(mm::configure::Build::Release)),
        std::string(mm::configure::build_link_flags(mm::configure::Build::Release)),
    };
    return settings;
}

mm::configure::Settings settings_with_cross(bool select_cross, std::string target,
                                            std::string invocation) {
    auto settings = host_settings();
    settings.target_compiler = select_cross ? mm::configure::CompilerSelection::Cross
                                            : mm::configure::CompilerSelection::Host;
    settings.cross = mm::configure::CompilerSettings{
        mm::configure::CompilerFamily::Gcc,
        std::move(invocation),
        std::move(target),
        "POSIX",
        "cross compile flags",
        "cross link flags",
    };
    settings.target_has_host_capability = true;
    settings.cross_runner = mm::configure::RunnerSettings{
        .invocation = "target-runner",
        .prefix_arguments = {"--sysroot", "/target"},
    };
    settings.cross_debugger = mm::configure::DebuggerSettings{
        .invocation = "target-debugger",
        .prefix_arguments = {"-q"},
        .connection = mm::configure::DebuggerConnection::RunnerRemote,
        .remote_endpoint = "localhost:1234",
        .runner_arguments = {"-g", "1234"},
    };
    settings.target_build_directory = "out-target-test";
    return settings;
}

void unconfigured_reports_the_shared_default() {
    const mm::test::scoped_tree tree{"model_configuration_default"};
    const auto quiet = mm::model::configuration(tree.root(), false);
    const auto loud = mm::model::configuration(tree.root(), true);
    expect(quiet != nullptr && loud != nullptr, "a tree with no configuration still resolves");
    if (quiet == nullptr || loud == nullptr) return;

    expect(!quiet->persisted(), "an absent out/config.mdy reports the default, not a configuration");
    expect(quiet->name() == "default", "the unconfigured lane is named default");
    expect(quiet->build() == models::Build::Debug, "the unconfigured build is debug");
    expect(quiet->compiler_family() == models::CompilerFamily::Gcc, "the unconfigured family is GCC");
    expect(quiet->compiler() == "g++", "the unconfigured C++ driver is g++");
    expect(quiet->selection() == models::CompilerSelection::Host, "the default selects the host");
    expect(quiet->build_directory() == "out" && quiet->host_build_directory() == "out",
           "an unconfigured build writes to out, where bootstrap already writes");
    expect(!quiet->verbose() && loud->verbose(), "verbose is reported as given");
}

void persisted_reports_the_written_lane() {
    const mm::test::scoped_tree tree{"model_configuration_persisted"};
    expect(mm::configure::write_configuration(tree.root(), host_settings()), "configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr, "a written configuration resolves");
    if (configuration == nullptr) return;

    expect(configuration->persisted(), "a written out/config.mdy is reported as persisted");
    expect(configuration->name() == "gcc-release", "the persisted name is reported");
    expect(configuration->build() == models::Build::Release, "the persisted build is reported");
    expect(configuration->compiler() == "g++-15", "the persisted driver is reported");
    expect(configuration->selection() == models::CompilerSelection::Host,
           "a native configuration selects the host compiler");
    expect(configuration->build_directory() == mm::configure::host_output_directory() &&
               configuration->host_build_directory() == mm::configure::host_output_directory(),
           "a configured build writes to the host lane, not to out");
    expect(configuration->compiler_flags() ==
               mm::configure::build_compile_flags(mm::configure::Build::Release),
           "flags come from the one shared build policy");

    const auto& toolchain = configuration->host_toolchain();
    const auto* compiler = toolchain.program(models::ToolRole::Compiler);
    expect(configuration->target_toolchain() == nullptr,
           "a host selection has no selected target toolchain");
    expect(compiler != nullptr && compiler == toolchain.program(models::ToolRole::Assembler) &&
               compiler == toolchain.program(models::ToolRole::Linker),
           "compiler, assembler, and linker share the configured driver");
    expect(toolchain.program(models::ToolRole::Librarian) == nullptr &&
               toolchain.program(models::ToolRole::Debugger) == nullptr,
           "librarian and debugger are unbound");
    expect(toolchain.invoked(models::ToolRole::Compiler) &&
               !toolchain.invoked(models::ToolRole::Assembler) &&
               toolchain.invoked(models::ToolRole::Linker) &&
               !toolchain.invoked(models::ToolRole::Librarian) &&
               !toolchain.invoked(models::ToolRole::Debugger),
           "only compiler and linker roles are invoked");
    expect(compiler != nullptr && compiler->name() == compiler->invocation().string() &&
               compiler->provenance() == models::Provenance::ThirdParty &&
               compiler->declared_by() == nullptr,
           "a configured driver is a manifest-free third-party tool named by its invocation");
}

void an_unselected_cross_record_is_not_a_target_toolchain() {
    const mm::test::scoped_tree tree{"model_configuration_unselected_cross"};
    const auto settings = settings_with_cross(false, "aarch64-linux-gnu", "aarch64-linux-gnu-g++");
    expect(mm::configure::write_configuration(tree.root(), settings), "configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr, "configuration resolves");
    if (configuration == nullptr) return;
    expect(configuration->selection() == models::CompilerSelection::Host &&
               configuration->target_toolchain() == nullptr &&
               configuration->target_has_host_capability(),
           "record presence does not override host selection");
    expect(configuration->configured_target_toolchain() != nullptr &&
               configuration->configured_target_toolchain()->target() == "aarch64-linux-gnu" &&
               configuration->target_build_directory() == "out-target-test",
           "an unselected target remains available for invocation projection");
    const auto* runner = configuration->configured_target_toolchain()->runner();
    expect(runner != nullptr && runner->program().invocation() == "target-runner" &&
               runner->prefix_arguments().size() == 2 && runner->forwards_arguments(),
           "the configured target exposes its execution runner");
    const auto* debugger = configuration->configured_target_toolchain()->debugger();
    expect(debugger != nullptr && debugger->program().invocation() == "target-debugger" &&
               debugger->connection() == models::DebuggerConnection::RunnerRemote &&
               debugger->remote_endpoint() == "localhost:1234" &&
               debugger->runner_arguments().size() == 2,
           "the configured target exposes its debugger orchestration");
}

void equal_targets_are_not_cross_compilation() {
    const mm::test::scoped_tree tree{"model_configuration_equal_targets"};
    const auto settings = settings_with_cross(true, "host", "cross-g++");
    expect(mm::configure::write_configuration(tree.root(), settings), "configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr && configuration->target_toolchain() != nullptr,
           "selected cross record is exposed as the target toolchain");
    if (configuration == nullptr || configuration->target_toolchain() == nullptr) return;
    expect(configuration->target_toolchain()->target() == configuration->host_toolchain().target(),
           "equal targets make the derived cross-compilation question false");
}

void equal_invocations_do_not_unify_toolchains() {
    const mm::test::scoped_tree tree{"model_configuration_equal_invocations"};
    const auto settings = settings_with_cross(true, "aarch64-linux-gnu", "g++-15");
    expect(mm::configure::write_configuration(tree.root(), settings), "configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr && configuration->target_toolchain() != nullptr,
           "selected cross record is exposed as the target toolchain");
    if (configuration == nullptr || configuration->target_toolchain() == nullptr) return;

    const auto* host = configuration->host_toolchain().program(models::ToolRole::Compiler);
    const auto* target = configuration->target_toolchain()->program(models::ToolRole::Compiler);
    expect(host != nullptr && target != nullptr && host->invocation() == target->invocation() &&
               host != target,
           "the same invocation has distinct ownership in distinct toolchains");
    expect(configuration->target_toolchain()->target() != configuration->host_toolchain().target(),
           "different targets make the derived cross-compilation question true");
}

// The host platform identity and locale/shell facts are fixed project policy.
void host_platform_locale_and_shell_are_fixed() {
    const mm::test::scoped_tree tree{"model_configuration_fixed"};
    const auto first = mm::model::configuration(tree.root(), false);
    const auto second = mm::model::configuration(tree.root(), true);
    expect(first != nullptr && second != nullptr, "both resolve");
    if (first == nullptr || second == nullptr) return;

    expect(first->host_platform().target() == "host" &&
               first->host_platform().system() == models::PlatformSystem::Posix,
           "expected the host platform to be POSIX");
    expect(first->locale() == "C", "expected locale() to be C");
    expect(first->shell() == "/bin/sh", "expected shell() to be /bin/sh");

    expect(second->host_platform().system() == first->host_platform().system(),
           "expected host platform not to vary with compiler or verbose");
    expect(second->locale() == first->locale(),
           "expected locale() not to vary with compiler or verbose");
    expect(second->shell() == first->shell(),
           "expected shell() not to vary with compiler or verbose");
}

void configured_platform_follows_lane_selection() {
    const mm::test::scoped_tree tree{"model_configuration_platform"};
    tree.manifest_raw("platforms/sdk",
                      "mm: 1.2\nkind: sdk\nname: arm-none-eabi-newlib\n"
                      "target: arm-none-eabi\ncompiler-family: gcc\nruntime: newlib\n");
    auto settings = settings_with_cross(false, "arm-none-eabi", "arm-none-eabi-g++");
    settings.configuration_2 = true;
    mm::configure::PlatformSettings platform;
    platform.target = "arm-none-eabi";
    platform.system = mm::configure::PlatformSystem::BareMetal;
    platform.runtime = mm::configure::PlatformRuntime::Newlib;
    platform.sdk = "arm-none-eabi-newlib";
    platform.sdk_manifest = "platforms/sdk/mm.mdy";
    platform.sdk_family = mm::configure::CompilerFamily::Gcc;
    platform.models_responsibilities = true;
    platform.responsibility_owners[mm::configure::Responsibility::RuntimeInit] =
        "arm-none-eabi-newlib";
    platform.responsibility_owners[mm::configure::Responsibility::Syscalls] =
        "arm-none-eabi-newlib";
    platform.unresolved = {
        mm::configure::Responsibility::ResetVector,
        mm::configure::Responsibility::InitialStack,
        mm::configure::Responsibility::MemoryLayout};
    settings.cross_platform = platform;
    expect(mm::configure::write_configuration(tree.root(), settings),
           "platform configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr, "platform configuration resolves");
    if (configuration == nullptr) return;
    expect(configuration->target_platform() == nullptr &&
               configuration->configured_target_platform() != nullptr,
           "selected and configured platform accessors mirror toolchains");
    const auto* configured = configuration->configured_target_platform();
    const auto sdk = configured->sdk();
    expect(configured->system() == models::PlatformSystem::BareMetal &&
               configured->runtime() == models::PlatformRuntime::Newlib &&
               sdk && *sdk == "arm-none-eabi-newlib" &&
               !configured->board() && configured->models_responsibilities() &&
               configured->unresolved().size() == 3,
           "configured platform exposes its SDK and responsibility state");
}

void toolchain_exposes_c_compiler() {
    const mm::test::scoped_tree tree{"model_c_compiler"};
    auto settings = host_settings();
    settings.host.c_compiler = "gcc-15";
    expect(mm::configure::write_configuration(tree.root(), settings), "configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr, "configuration resolves");
    if (configuration == nullptr) return;

    const auto* c_comp = configuration->host_toolchain().c_compiler();
    expect(c_comp != nullptr, "host c_compiler is exposed");
    if (c_comp != nullptr) {
        expect(c_comp->invocation() == "gcc-15", "c_compiler invocation matches");
        expect(configuration->host_toolchain().program(models::ToolRole::CCompiler) == c_comp,
               "program(ToolRole::CCompiler) matches c_compiler()");
        expect(c_comp->provenance() == models::Provenance::ThirdParty,
               "c_compiler provenance is ThirdParty");
    }

    const mm::test::scoped_tree default_tree{"model_c_compiler_default"};
    const auto unconfig = mm::model::configuration(default_tree.root(), false);
    expect(unconfig != nullptr, "unconfigured resolves");
    if (unconfig != nullptr) {
        expect(unconfig->host_toolchain().c_compiler() == nullptr,
               "unconfigured host has null c_compiler");
        expect(unconfig->host_toolchain().program(models::ToolRole::CCompiler) == nullptr,
               "unconfigured host ToolRole::CCompiler is null");
    }
}

void platform_exposes_link_ownership() {
    const mm::test::scoped_tree tree{"model_link_ownership"};
    tree.manifest_raw("platforms/sdk",
                      "mm: 1.2\nkind: sdk\nname: cmake-demo\n"
                      "target: m68k-linux-gnu\ncompiler-family: gcc\nruntime: none\n");

    auto settings = settings_with_cross(true, "m68k-linux-gnu", "m68k-linux-gnu-g++");
    settings.configuration_2 = true;
    mm::configure::PlatformSettings platform;
    platform.target = "m68k-linux-gnu";
    platform.system = mm::configure::PlatformSystem::Linux;
    platform.runtime = mm::configure::PlatformRuntime::None;
    platform.link_ownership = mm::configure::LinkOwnership::External;
    platform.sdk = "cmake-demo";
    platform.sdk_manifest = "platforms/sdk/mm.mdy";
    platform.sdk_family = mm::configure::CompilerFamily::Gcc;
    settings.cross_platform = platform;
    expect(mm::configure::write_configuration(tree.root(), settings),
           "platform configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr, "configuration resolves");
    if (configuration == nullptr) return;

    expect(configuration->host_platform().link_ownership() == models::LinkOwnership::Project,
           "host platform link ownership is always Project");
    const auto* target_plat = configuration->target_platform();
    expect(target_plat != nullptr, "target platform is present");
    if (target_plat != nullptr) {
        expect(target_plat->link_ownership() == models::LinkOwnership::External,
               "target platform exposes LinkOwnership::External");
    }
}

void configured_platform_exposes_effective_provider() {
    const mm::test::scoped_tree tree{"model_platform_providers"};
    tree.manifest("", "kind: project\nname: p\nfolder: iface\nfolder: sdk-provider\n"
                      "folder: board-provider\nfolder: sdk\nfolder: board\n");
    tree.manifest_raw("iface", "mm: 1.2\nkind: module\nname: iface\nmodule: mm.iface\n"
                               "file: iface.cppm\nplatform-interface:\n");
    tree.manifest_raw("sdk-provider", "mm: 1.2\nkind: module\nname: sdk-provider\n"
                                      "module: platform.sdk.iface\nuse: mm.iface\n"
                                      "file: provider.cppm\n");
    tree.manifest_raw("board-provider", "mm: 1.2\nkind: module\nname: board-provider\n"
                                        "module: platform.board.iface\nuse: mm.iface\n"
                                        "file: provider.cppm\n");
    tree.manifest_raw("sdk", "mm: 1.2\nkind: sdk\nname: demo-sdk\n"
                             "target: arm-none-eabi\ncompiler-family: gcc\nruntime: newlib\n"
                             "platform-provider: mm.iface platform.sdk.iface\n");
    tree.manifest_raw("board", "mm: 1.2\nkind: board\nname: demo-board\n"
                               "sdk: demo-sdk\ncpu: cortex-m0plus\ninstruction-set: thumb\n"
                               "float-abi: soft\nlinker-script: link.ld\nfile: vectors.cpp\n"
                               "platform-provider: mm.iface platform.board.iface\n");
    std::ofstream(tree.root() / "board/link.ld") << "/* fixture */\n";
    std::ofstream(tree.root() / "board/vectors.cpp") << "// fixture\n";

    auto settings = settings_with_cross(true, "arm-none-eabi", "arm-none-eabi-g++");
    settings.configuration_2 = true;
    mm::configure::PlatformSettings platform;
    platform.target = "arm-none-eabi";
    platform.system = mm::configure::PlatformSystem::BareMetal;
    platform.runtime = mm::configure::PlatformRuntime::Newlib;
    platform.sdk = "demo-sdk";
    platform.sdk_manifest = "sdk/mm.mdy";
    platform.board = "demo-board";
    platform.board_manifest = "board/mm.mdy";
    platform.sdk_family = mm::configure::CompilerFamily::Gcc;
    platform.linker_script = "board/link.ld";
    platform.board_sources = {"board/vectors.cpp"};
    platform.compiler_arguments = {"-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft"};
    platform.models_responsibilities = true;
    platform.responsibility_owners[mm::configure::Responsibility::ResetVector] = "demo-board";
    platform.responsibility_owners[mm::configure::Responsibility::InitialStack] = "demo-board";
    platform.responsibility_owners[mm::configure::Responsibility::MemoryLayout] = "demo-board";
    platform.responsibility_owners[mm::configure::Responsibility::RuntimeInit] = "demo-sdk";
    platform.responsibility_owners[mm::configure::Responsibility::Syscalls] = "demo-sdk";
    settings.cross_platform = platform;
    expect(mm::configure::write_configuration(tree.root(), settings),
           "provider platform configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr && configuration->target_platform() != nullptr,
           "selected provider platform resolves");
    if (configuration == nullptr || configuration->target_platform() == nullptr) return;

    const auto providers = configuration->target_platform()->platform_providers();
    expect(providers.size() == 1 && providers.front().interface_module == "mm.iface" &&
               providers.front().provider_module == "platform.board.iface" &&
               providers.front().owner == "demo-board" && providers.front().from_board,
           "model exposes the effective board override and its owner");
}

const mm::test::case_ cases[] = {
    { "unconfigured reports the shared default", &unconfigured_reports_the_shared_default },
    { "persisted reports the written lane",      &persisted_reports_the_written_lane },
    { "unselected cross is not a target",        &an_unselected_cross_record_is_not_a_target_toolchain },
    { "equal targets are not cross compilation", &equal_targets_are_not_cross_compilation },
    { "equal invocations remain separate",       &equal_invocations_do_not_unify_toolchains },
    { "host platform, locale and shell are fixed", &host_platform_locale_and_shell_are_fixed },
    { "configured platform follows selection",  &configured_platform_follows_lane_selection },
    { "toolchain exposes C compiler",           &toolchain_exposes_c_compiler },
    { "platform exposes link ownership",        &platform_exposes_link_ownership },
    { "platform exposes effective provider",    &configured_platform_exposes_effective_provider },
};

const mm::test::registrar reg{"mm.model configuration", cases};

}  // namespace
