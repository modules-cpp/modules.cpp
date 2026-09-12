// Black box tests for mm.configure's atomic config.mdy writer.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.configure;
import mm.mdy;
import mm.test;

namespace {

std::string first(const mm::mdy::MDYDocument& document, std::string_view key) {
    const auto found = document.metadata.find(key);
    return found == document.metadata.end() || found->second.empty() ? std::string{}
                                                                    : found->second.front();
}

std::vector<std::string> all(const mm::mdy::MDYDocument& document, std::string_view key) {
    const auto found = document.metadata.find(key);
    return found == document.metadata.end() ? std::vector<std::string>{} : found->second;
}

mm::configure::Settings native_settings() {
    mm::configure::Settings settings;
    settings.host = {
        mm::configure::CompilerFamily::Gcc,
        "c++",
        "host",
        "POSIX",
        "-std=c++20 -fmodules-ts -x c++",
        "-std=c++20",
    };
    return settings;
}

void writes_a_native_configuration() {
    const mm::test::scoped_tree tree{"configure_native"};
    const auto settings = native_settings();

    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected native config.mdy write to succeed");

    const auto document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(document.status == mm::mdy::ParseStatus::Ok,
                     "expected written config.mdy to be readable");
    mm::test::expect(first(document, "kind") == "configuration",
                     "expected kind: configuration");
    mm::test::expect(first(document, "build") == "debug", "expected debug build to round trip");
    mm::test::expect(first(document, "target-compiler") == "host",
                     "expected native configuration to select host");
    mm::test::expect(first(document, "host-compiler") == "c++",
                     "expected host compiler to round trip");
    mm::test::expect(first(document, "host-compiler-family") == "gcc",
                     "expected host compiler family to round trip");
    mm::test::expect(first(document, "host-build-directory") == "out-host" &&
                         first(document, "target-build-directory") == "out-host",
                     "expected native build directories to round trip");
    mm::test::expect(!std::filesystem::exists(tree.root() / "out" / "config.mdy.tmp"),
                     "expected atomic rename not to leave its temporary file");
    mm::test::expect(!std::filesystem::exists(tree.root() / "config.mdy"),
                     "expected no configuration file at the project root");
}

void writes_a_cross_configuration() {
    const mm::test::scoped_tree tree{"configure_cross"};
    auto settings = native_settings();
    settings.name = "m68k-linux-gnu";
    settings.target_compiler = mm::configure::CompilerSelection::Cross;
    settings.target_has_host_capability = true;
    settings.cross_runner = mm::configure::runner_profile("qemu-user", "m68k-linux-gnu");
    settings.cross_debugger = mm::configure::debugger_profile("gdb", "m68k-linux-gnu");
    settings.cross = mm::configure::CompilerSettings{
        mm::configure::CompilerFamily::Gcc,
        "m68k-linux-gnu-g++",
        "m68k-linux-gnu",
        "POSIX",
        "-std=c++20 -fmodules-ts -x c++",
        "-std=c++20",
    };
    settings.target_build_directory = "out/target/m68k-linux-gnu";

    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected cross config.mdy write to succeed");

    const auto document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "target-compiler") == "cross",
                     "expected cross configuration to select cross");
    mm::test::expect(first(document, "target-host-capability") == "yes",
                     "expected target host capability to round trip");
    mm::test::expect(first(document, "cross-runner") == "qemu-m68k" &&
                         first(document, "cross-runner-image") == "positional",
                     "expected runner profile to be persisted");
    mm::test::expect(first(document, "cross-debugger") == "gdb-multiarch" &&
                         first(document, "cross-debugger-connection") == "runner-remote" &&
                         first(document, "cross-debugger-remote-endpoint") ==
                             "localhost:1234",
                     "expected debugger profile to be persisted");
    mm::test::expect(first(document, "cross-compiler") == "m68k-linux-gnu-g++",
                     "expected cross compiler to round trip");
    mm::test::expect(first(document, "target-build-directory") ==
                         "out/target/m68k-linux-gnu",
                     "expected cross target build directory to round trip");
}

void parses_supported_compiler_selectors() {
    const auto gcc = mm::configure::parse_compiler("gcc-15");
    mm::test::expect(gcc && gcc->family == mm::configure::CompilerFamily::Gcc &&
                         gcc->invocation == "g++-15" && gcc->requested_major.value_or(0) == 15,
                     "expected gcc-15 to select the versioned C++ driver");

    const auto gxx = mm::configure::parse_compiler("g++-16");
    mm::test::expect(gxx && gxx->family == mm::configure::CompilerFamily::Gcc &&
                         gxx->invocation == "g++-16" && gxx->requested_major.value_or(0) == 16,
                     "expected g++-16 to remain unchanged");

    const auto clang = mm::configure::parse_compiler("clang++-20");
    mm::test::expect(clang && clang->family == mm::configure::CompilerFamily::Clang &&
                           clang->invocation == "clang++-20" &&
                           clang->requested_major.value_or(0) == 20,
                     "expected clang++-20 to select Clang");

    const auto cross = mm::configure::parse_compiler("aarch64-linux-gnu-gcc-15");
    mm::test::expect(cross && cross->family == mm::configure::CompilerFamily::Gcc &&
                         cross->invocation == "aarch64-linux-gnu-g++-15" &&
                         cross->target_prefix == "aarch64-linux-gnu",
                     "expected a target GCC C driver to normalize to its C++ driver");

    mm::test::expect(mm::configure::valid_target_triple("aarch64-linux-gnu") &&
                         !mm::configure::valid_target_triple("host") &&
                         !mm::configure::valid_target_triple("../target") &&
                         !mm::configure::valid_target_triple("bad target"),
                     "expected target triples to reject paths and whitespace");
}

void rejects_invalid_compiler_selectors() {
    mm::test::expect(!mm::configure::parse_compiler("c++"),
                     "expected an ambiguous compiler name to fail");
    mm::test::expect(!mm::configure::parse_compiler("gcc-latest"),
                     "expected a nonnumeric GCC version to fail");
    mm::test::expect(!mm::configure::parse_compiler("clang++-0"),
                     "expected compiler version zero to fail");
}

void parses_supported_builds() {
    const auto debug = mm::configure::parse_build("debug");
    const auto release = mm::configure::parse_build("release");
    mm::test::expect(debug && *debug == mm::configure::Build::Debug,
                     "expected debug build to parse");
    mm::test::expect(release && *release == mm::configure::Build::Release,
                     "expected release build to parse");
    mm::test::expect(mm::configure::build_compile_flags(*debug) == "-std=c++20 -O0 -g" &&
                         mm::configure::build_link_flags(*debug) == "-std=c++20 -g",
                     "expected debug build flags");
    mm::test::expect(mm::configure::build_compile_flags(*release) ==
                             "-std=c++20 -O2 -DNDEBUG" &&
                         mm::configure::build_link_flags(*release) == "-std=c++20 -O2",
                     "expected release build flags");
    mm::test::expect(!mm::configure::parse_build("optimized"),
                     "expected an unsupported build to fail");
}

void invalid_settings_leave_the_existing_file_unchanged() {
    const mm::test::scoped_tree tree{"configure_preserve"};
    std::error_code ec;
    std::filesystem::create_directories(tree.root() / "out", ec);
    const auto path = tree.root() / "out" / "config.mdy";
    {
        std::ofstream existing(path);
        existing << "existing configuration\n";
    }

    auto settings = native_settings();
    settings.name = "invalid\nname";
    mm::test::expect(!mm::configure::write_configuration(tree.root(), settings),
                     "expected an injected newline to be rejected");

    std::ifstream existing(path);
    std::string text;
    std::getline(existing, text);
    mm::test::expect(text == "existing configuration",
                     "expected a failed write to preserve config.mdy");

    settings = native_settings();
    settings.target_has_host_capability = true;
    mm::test::expect(!mm::configure::write_configuration(tree.root(), settings),
                     "expected host capability without a target to be rejected");
}

std::string capture_configuration_log(const mm::configure::ConfigurationLog& log, bool& ok) {
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    ok = mm::configure::log_configuration(log);
    std::cout.rdbuf(previous);
    return output.str();
}

void logs_default_and_verbose_configurations() {
    const mm::test::scoped_tree tree{"configure_log"};
    const auto path = tree.root() / "out" / "config.mdy";

    bool ok = false;
    auto output = capture_configuration_log(
        {
            .tool = "test",
            .configuration_path = path,
            .build = "debug",
            .compiler_family = "gcc",
            .compiler = "g++-15",
            .compile_flags = "-std=c++20",
            .link_flags = "-std=c++20",
            .target = "out/tests/example",
        },
        ok);
    mm::test::expect(ok, "expected logging a default configuration to succeed");
    mm::test::expect(output ==
                         "  configuration default\n"
                         "  target out/tests/example\n",
                     "expected nonverbose logging to hide compiler details");

    const auto settings = native_settings();
    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected configuration for logging to be written");

    output = capture_configuration_log(
        {
            .tool = "build",
            .configuration_path = path,
            .build = "release",
            .compiler_family = "gcc",
            .compiler = "g++-15",
            .compile_flags = "-std=c++20",
            .link_flags = "-std=c++20",
            .runner = "qemu-m68k",
            .debugger = "gdb-multiarch",
            .target = "out-host",
            .verbose = true,
        },
        ok);
    mm::test::expect(ok, "expected logging a persisted configuration to succeed");
    mm::test::expect(output == "  configuration " + path.string() +
                                   "\n"
                                   "    build         release\n"
                                   "    family        gcc\n"
                                   "    compiler      g++-15\n"
                                   "    compile flags -std=c++20\n"
                                   "    link flags    -std=c++20\n"
                                   "    runner        qemu-m68k\n"
                                   "    debugger      gdb-multiarch\n"
                                   "  target out-host\n",
                     "expected verbose logging to print shared configuration details");
}

void openocd_runner_profile_and_configuration() {
    mm::configure::PlatformSettings platform;
    platform.target = "arm-none-eabi";
    platform.machine = "rp2040";

    const auto openocd = mm::configure::runner_profile("openocd", "arm-none-eabi", &platform);
    mm::test::expect(openocd.has_value(), "expected openocd profile to resolve for rp2040");
    mm::test::expect(openocd->invocation == "openocd", "expected openocd invocation");
    mm::test::expect(openocd->image == mm::configure::RunnerImage::Option, "expected option image");
    mm::test::expect(openocd->image_option == "-c \"program ... verify reset\"", "expected image option template");
    mm::test::expect(openocd->image_arguments == std::vector<std::string>{"-c", "program {} verify reset"},
                     "expected openocd image_arguments");
    mm::test::expect(!openocd->forwards_arguments, "expected forwards_arguments to be false");

    platform.machine = "rp2350";
    mm::test::expect(mm::configure::runner_profile("openocd", "arm-none-eabi", &platform).has_value(),
                     "expected openocd profile to resolve for rp2350");

    platform.machine = "mps2-an385";
    mm::test::expect(!mm::configure::runner_profile("openocd", "arm-none-eabi", &platform).has_value(),
                     "expected openocd profile to reject mps2-an385");

    platform.machine = "rp2040";
    mm::test::expect(!mm::configure::runner_profile("qemu-system", "arm-none-eabi", &platform).has_value(),
                     "expected qemu-system profile to reject rp2040");

    const auto openocd_debugger = mm::configure::debugger_profile("gdb", "arm-none-eabi", &platform);
    mm::test::expect(openocd_debugger.has_value(), "expected openocd debugger profile to resolve for rp2040");
    mm::test::expect(openocd_debugger->invocation == "gdb-multiarch", "expected gdb-multiarch invocation");
    mm::test::expect(openocd_debugger->connection == mm::configure::DebuggerConnection::RunnerRemote,
                     "expected runner-remote connection");
    mm::test::expect(openocd_debugger->remote_endpoint == "localhost:3333",
                     "expected localhost:3333 remote endpoint");
    mm::test::expect(openocd_debugger->runner_arguments.empty(), "expected empty runner_arguments");
    mm::test::expect(openocd_debugger->prefix_arguments == std::vector<std::string>{"-q"},
                     "expected -q prefix argument");

    mm::test::expect(mm::configure::debugger_profile("openocd", "arm-none-eabi", &platform).has_value(),
                     "expected openocd named debugger profile to resolve for rp2040");

    platform.machine = "rp2350";
    mm::test::expect(mm::configure::debugger_profile("gdb", "arm-none-eabi", &platform).has_value(),
                     "expected openocd debugger profile to resolve for rp2350");

    platform.machine = "mps2-an385";
    mm::test::expect(!mm::configure::debugger_profile("gdb", "arm-none-eabi", &platform).has_value(),
                     "expected openocd debugger profile to reject mps2-an385");

    const mm::test::scoped_tree tree{"configure_openocd"};
    auto settings = native_settings();
    settings.name = "arm-none-eabi";
    settings.target_compiler = mm::configure::CompilerSelection::Cross;
    platform.machine = "rp2040";
    settings.cross_platform = platform;
    settings.cross_runner = openocd;
    settings.cross_debugger = openocd_debugger;
    settings.cross = mm::configure::CompilerSettings{
        mm::configure::CompilerFamily::Gcc,
        "arm-none-eabi-g++",
        "arm-none-eabi",
        "bare-metal",
        "-std=c++20",
        "-std=c++20",
    };
    settings.target_build_directory = "out-target-arm-none-eabi";

    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected openocd cross config.mdy write to succeed");

    const auto document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "cross-runner") == "openocd", "expected cross-runner openocd");
    mm::test::expect(first(document, "cross-runner-image") == "option", "expected cross-runner-image option");
    mm::test::expect(first(document, "cross-runner-image-option") == "-c \"program ... verify reset\"",
                     "expected cross-runner-image-option template");
    const std::vector<std::string> expected_image_args = {"-c", "program {} verify reset"};
    mm::test::expect(all(document, "cross-runner-image-argument") == expected_image_args,
                     "expected cross-runner-image-argument entries");
    mm::test::expect(first(document, "cross-runner-forwards-arguments") == "no",
                     "expected cross-runner-forwards-arguments no");
    mm::test::expect(first(document, "cross-debugger") == "gdb-multiarch",
                     "expected cross-debugger gdb-multiarch");
    mm::test::expect(first(document, "cross-debugger-connection") == "runner-remote",
                     "expected cross-debugger-connection runner-remote");
    mm::test::expect(first(document, "cross-debugger-remote-endpoint") == "localhost:3333",
                     "expected cross-debugger-remote-endpoint localhost:3333");
    mm::test::expect(all(document, "cross-debugger-prefix-argument") == std::vector<std::string>{"-q"},
                     "expected cross-debugger-prefix-argument -q");
}

void derives_candidate_c_compiler() {
    using mm::configure::candidate_c_compiler;

    mm::test::expect(candidate_c_compiler("g++") == "gcc",
                     "g++ derives gcc");
    mm::test::expect(candidate_c_compiler("g++-15") == "gcc-15",
                     "g++-15 derives gcc-15");
    mm::test::expect(candidate_c_compiler("clang++") == "clang",
                     "clang++ derives clang");
    mm::test::expect(candidate_c_compiler("clang++-18") == "clang-18",
                     "clang++-18 derives clang-18");
    mm::test::expect(candidate_c_compiler("arm-none-eabi-g++") == "arm-none-eabi-gcc",
                     "arm-none-eabi-g++ derives arm-none-eabi-gcc");
    mm::test::expect(candidate_c_compiler("arm-none-eabi-g++-14") == "arm-none-eabi-gcc-14",
                     "arm-none-eabi-g++-14 derives arm-none-eabi-gcc-14");
    mm::test::expect(candidate_c_compiler("arm-none-eabi-clang++") == "arm-none-eabi-clang",
                     "arm-none-eabi-clang++ derives arm-none-eabi-clang");
    mm::test::expect(candidate_c_compiler("arm-none-eabi-clang++-18") == "arm-none-eabi-clang-18",
                     "arm-none-eabi-clang++-18 derives arm-none-eabi-clang-18");
    mm::test::expect(candidate_c_compiler("x86_64-w64-mingw32-g++-posix") == "x86_64-w64-mingw32-gcc-posix",
                     "mingw g++-posix derives gcc-posix");
    mm::test::expect(candidate_c_compiler("custom-cxx").empty(),
                     "unknown driver returns empty string");
}

bool fixture_driver_command(const std::string& command, std::string& output) {
    if (command.find("nonexistent-compiler-xyz-123") != std::string::npos) return false;
    if (command.ends_with(" -dumpmachine")) output = "x86_64-linux-gnu";
    else if (command.ends_with(" -dumpversion")) output = "16";
    else if (command.ends_with(" --version")) output = "gcc fixture 16";
    else return false;
    return true;
}

void probes_c_compiler() {
    using mm::configure::CompilerFamily;
    using mm::configure::probe_compiler;
    using mm::configure::probe_c_compiler;

    std::string err;
    const auto probe = probe_compiler("gcc", fixture_driver_command);
    if (probe.has_value()) {
        mm::test::expect(probe_c_compiler("gcc", "g++", CompilerFamily::Gcc, err,
                                         fixture_driver_command),
                         "matching C compiler probe succeeds");

        mm::test::expect(!probe_c_compiler("gcc", "g++", CompilerFamily::Clang, err,
                                          fixture_driver_command),
                         "family mismatch fails");
        mm::test::expect(err.find("family") != std::string::npos,
                         "diagnostic mentions family");
    }

    mm::test::expect(!probe_c_compiler("nonexistent-compiler-xyz-123", "g++",
                                      CompilerFamily::Gcc, err, fixture_driver_command),
                     "missing compiler fails to probe");
    mm::test::expect(err.find("cannot probe") != std::string::npos,
                     "diagnostic mentions probe failure");
}

void writes_and_preserves_c_compiler_configuration() {
    const mm::test::scoped_tree tree{"configure_c_compiler"};
    auto settings = native_settings();
    settings.host.c_compiler = "gcc";
    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected native config with c_compiler write to succeed");

    auto document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "host-c-compiler") == "gcc",
                     "expected host-c-compiler gcc");

    settings.name = "m68k-linux-gnu";
    settings.target_compiler = mm::configure::CompilerSelection::Cross;
    settings.target_build_directory = "out-target-m68k-linux-gnu";
    settings.cross = mm::configure::CompilerSettings{
        mm::configure::CompilerFamily::Gcc,
        "m68k-linux-gnu-g++",
        "m68k-linux-gnu",
        "POSIX",
        "-std=c++20",
        "-std=c++20",
        "m68k-linux-gnu-gcc",
    };
    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected cross config with c_compiler write to succeed");

    document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "host-c-compiler") == "gcc",
                     "expected host-c-compiler preserved");
    mm::test::expect(first(document, "cross-c-compiler") == "m68k-linux-gnu-gcc",
                     "expected cross-c-compiler m68k-linux-gnu-gcc");
}

void writes_cross_link_external_configuration() {
    const mm::test::scoped_tree tree{"configure_cross_link_external"};
    auto settings = native_settings();
    settings.name = "pico-arm";
    settings.configuration_2 = true;
    settings.target_compiler = mm::configure::CompilerSelection::Cross;
    settings.target_build_directory = "out-target-arm-none-eabi";
    settings.cross = mm::configure::CompilerSettings{
        mm::configure::CompilerFamily::Gcc,
        "arm-none-eabi-g++",
        "arm-none-eabi",
        "POSIX",
        "-std=c++20",
        "-std=c++20",
        "arm-none-eabi-gcc",
    };

    mm::configure::PlatformSettings platform;
    platform.target = "arm-none-eabi";
    platform.system = mm::configure::PlatformSystem::BareMetal;
    platform.runtime = mm::configure::PlatformRuntime::None;
    platform.link_ownership = mm::configure::LinkOwnership::External;
    platform.sdk = "pico-arm";
    platform.sdk_manifest = "platforms/pico/sdk/pico-arm/mm.mdy";
    platform.sdk_family = mm::configure::CompilerFamily::Gcc;
    platform.board = "pico";
    platform.board_manifest = "platforms/pico/pico/mm.mdy";
    platform.machine = "rp2040";
    platform.linker_script = "should-be-omitted.ld";
    platform.board_sources = {"should-be-omitted.cpp"};
    platform.compiler_arguments = {"-mcpu=cortex-m0plus"};
    settings.cross_platform = platform;

    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected external-link board configuration to write");

    auto document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "cross-link") == "external",
                     "expected cross-link: external");
    mm::test::expect(first(document, "cross-board") == "pico",
                     "expected cross-board: pico");
    mm::test::expect(first(document, "cross-board-machine") == "rp2040",
                     "expected cross-board-machine: rp2040");
    mm::test::expect(first(document, "cross-board-linker-script").empty(),
                     "expected cross-board-linker-script omitted for external link");
    mm::test::expect(all(document, "cross-board-source").empty(),
                     "expected cross-board-source omitted for external link");

    // Boardless hosted external lane
    settings.name = "m68k-linux-external";
    settings.target_build_directory = "out-target-m68k-linux-gnu";
    settings.cross->invocation = "m68k-linux-gnu-g++";
    settings.cross->target = "m68k-linux-gnu";
    settings.cross->c_compiler = "m68k-linux-gnu-gcc";
    platform.target = "m68k-linux-gnu";
    platform.system = mm::configure::PlatformSystem::Linux;
    platform.runtime = mm::configure::PlatformRuntime::None;
    platform.link_ownership = mm::configure::LinkOwnership::External;
    platform.sdk = "cmake-demo";
    platform.sdk_manifest = "platforms/m68k-linux-external/mm.mdy";
    platform.board.reset();
    platform.board_manifest.reset();
    platform.machine.clear();
    platform.linker_script.clear();
    platform.board_sources.clear();
    platform.compiler_arguments.clear();
    settings.cross_platform = platform;

    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected boardless external lane configuration to write");

    document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "cross-link") == "external",
                     "expected cross-link: external in boardless lane");
    mm::test::expect(first(document, "cross-board").empty(),
                     "expected no cross-board in boardless lane");
    mm::test::expect(first(document, "cross-board-manifest").empty(),
                     "expected no cross-board-manifest in boardless lane");
    mm::test::expect(first(document, "cross-board-machine").empty(),
                     "expected no cross-board-machine in boardless lane");
    mm::test::expect(first(document, "cross-board-linker-script").empty(),
                     "expected no cross-board-linker-script in boardless lane");
    mm::test::expect(all(document, "cross-board-source").empty(),
                     "expected no cross-board-source in boardless lane");
}

const mm::test::case_ cases[] = {
    {"writes a native configuration", &writes_a_native_configuration},
    {"writes a cross configuration", &writes_a_cross_configuration},
    {"openocd runner profile and configuration", &openocd_runner_profile_and_configuration},
    {"invalid settings preserve existing config", &invalid_settings_leave_the_existing_file_unchanged},
    {"parses supported compiler selectors", &parses_supported_compiler_selectors},
    {"rejects invalid compiler selectors", &rejects_invalid_compiler_selectors},
    {"parses supported builds", &parses_supported_builds},
    {"logs default and verbose configurations", &logs_default_and_verbose_configurations},
    {"derives candidate C compiler", &derives_candidate_c_compiler},
    {"probes C compiler", &probes_c_compiler},
    {"writes and preserves C compiler configuration", &writes_and_preserves_c_compiler_configuration},
    {"writes cross-link external configuration", &writes_cross_link_external_configuration},
};

const mm::test::registrar reg{"mm.configure write_configuration", cases};

}  // namespace
