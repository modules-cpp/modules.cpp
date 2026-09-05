// Black box tests for mm.configure's atomic config.mdy writer.

#include <filesystem>
#include <fstream>
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
    mm::test::expect(first(document, "target-compiler") == "host",
                     "expected native configuration to select host");
    mm::test::expect(first(document, "host-compiler") == "c++",
                     "expected host compiler to round trip");
    mm::test::expect(first(document, "host-build-directory") == "out/host" &&
                         first(document, "target-build-directory") == "out/host",
                     "expected native build directories to round trip");
    mm::test::expect(!std::filesystem::exists(tree.root() / "out" / "config.mdy.tmp"),
                     "expected atomic rename not to leave its temporary file");
    mm::test::expect(!std::filesystem::exists(tree.root() / "config.mdy"),
                     "expected no configuration file at the project root");
}

void writes_a_cross_configuration() {
    const mm::test::scoped_tree tree{"configure_cross"};
    auto settings = native_settings();
    settings.name = "aarch64-linux-gnu";
    settings.target_compiler = mm::configure::CompilerSelection::Cross;
    settings.cross = mm::configure::CompilerSettings{
        "aarch64-linux-gnu-g++",
        "aarch64-linux-gnu",
        "POSIX",
        "-std=c++20 -fmodules-ts -x c++",
        "-std=c++20",
    };
    settings.target_build_directory = "out/target/aarch64-linux-gnu";

    mm::test::expect(mm::configure::write_configuration(tree.root(), settings),
                     "expected cross config.mdy write to succeed");

    const auto document = mm::mdy::Parser::parse_file(tree.root() / "out" / "config.mdy");
    mm::test::expect(first(document, "target-compiler") == "cross",
                     "expected cross configuration to select cross");
    mm::test::expect(first(document, "cross-compiler") == "aarch64-linux-gnu-g++",
                     "expected cross compiler to round trip");
    mm::test::expect(first(document, "target-build-directory") ==
                         "out/target/aarch64-linux-gnu",
                     "expected cross target build directory to round trip");
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
}

const mm::test::case_ cases[] = {
    {"writes a native configuration", &writes_a_native_configuration},
    {"writes a cross configuration", &writes_a_cross_configuration},
    {"invalid settings preserve existing config", &invalid_settings_leave_the_existing_file_unchanged},
};

const mm::test::registrar reg{"mm.configure write_configuration", cases};

}  // namespace
