// Black box tests for mm.configure's atomic config.mdy writer.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

import mm.configure;
import mm.mdy;
import mm.test;

namespace {

std::string first(const mm::mdy::MDYDocument& document, std::string_view key) {
    const auto found = document.metadata.find(key);
    return found == document.metadata.end() || found->second.empty() ? std::string{}
                                                                    : found->second.front();
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

const mm::test::case_ cases[] = {
    {"writes a native configuration", &writes_a_native_configuration},
    {"writes a cross configuration", &writes_a_cross_configuration},
    {"invalid settings preserve existing config", &invalid_settings_leave_the_existing_file_unchanged},
    {"parses supported compiler selectors", &parses_supported_compiler_selectors},
    {"rejects invalid compiler selectors", &rejects_invalid_compiler_selectors},
    {"parses supported builds", &parses_supported_builds},
    {"logs default and verbose configurations", &logs_default_and_verbose_configurations},
};

const mm::test::registrar reg{"mm.configure write_configuration", cases};

}  // namespace
