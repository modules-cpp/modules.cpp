// Installed-tool integration: all effects are confined to a disposable project.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

import mm.build;
import mm.test;

namespace {
using mm::test::expect;

std::string read_text(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

int invoke(const std::filesystem::path& binary, std::string_view arguments,
           const std::filesystem::path& log) {
    return mm::build::run(mm::build::default_toolchain(), mm::build::shell_quote(binary) +
                         " " + std::string(arguments) + " > " + mm::build::shell_quote(log) + " 2>&1");
}

void installed_tools_support_11() {
    std::error_code ec;
    const auto repository = std::filesystem::current_path(ec);
    expect(!ec, "repository directory available");
    const auto bin = repository / "out/bin";
    const mm::test::scoped_tree tree{"options_cli"};
    tree.manifest_raw("", "mm: 1.1\nkind: project\nname: fixture\nfolder: app\nfolder: test\noption: warnings yes\nread-only: warnings\n");
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\nreset: optimize\nread-only: optimize\n");
    tree.manifest("test", "kind: test\nname: example_test\nunit: app/main.cpp\n");
    std::ofstream(tree.root() / "app/main.cpp") << "int main() { return 0; }\n";
    std::filesystem::create_directories(tree.root() / "tools/check/cppcheck", ec);
    expect(!ec, "check prerequisite directory created");
    std::filesystem::copy_file(repository / "tools/check/cppcheck/cpp20_rules.py",
                               tree.root() / "tools/check/cppcheck/cpp20_rules.py", ec);
    expect(!ec, "check addon made available to fixture");

    const auto root_arg = mm::build::shell_quote(tree.root());
    const auto child_arg = mm::build::shell_quote(tree.root() / "app");
    const auto log = tree.root() / "tool.log";
    const auto record_path = tree.root() / "out-host/app/resolved-options.mdy";
    const auto config_path = tree.root() / "out/config.mdy";
    expect(invoke(bin / "configure", "--build release -v " + root_arg, log) == 0, "configure accepts 1.1 tree");
    const auto release_record = read_text(record_path);
    expect(release_record.find("option: optimize 2") != std::string::npos &&
           release_record.find("read-only: warnings") != std::string::npos, "release record reflects reset and inherited lock");
    expect(read_text(log).find("read-only from") != std::string::npos, "verbose output includes lock source");
    expect(invoke(bin / "configure", "--build release " + child_arg, log) == 0, "descendant configure locates project root");
    expect(read_text(record_path) == release_record, "subtree invocation produces identical record");
    expect(invoke(bin / "configure", "--build debug " + root_arg, log) == 0, "debug replaces same record tree");
    expect(read_text(record_path).find("option: optimize 0") != std::string::npos, "reset recomputed for debug");

    const auto stable_config = read_text(config_path);
    const auto stable_record = read_text(record_path);
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: warnings yes\n");
    expect(invoke(bin / "configure", "--build release " + child_arg, log) == 65, "descendant cannot bypass lock even with equal value");
    expect(read_text(config_path) == stable_config && read_text(record_path) == stable_record, "semantic error publishes nothing");
    expect(read_text(log).find("locked by mm.mdy") != std::string::npos, "lock conflict names original manifest");
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: future-feature yes\n");
    expect(invoke(bin / "configure", root_arg, log) == 65, "configure rejects unknown option");
    expect(read_text(config_path) == stable_config && read_text(record_path) == stable_record, "unknown option publishes nothing");
    tree.manifest("orphan", "kind: dir\nname: orphan\n");
    expect(invoke(bin / "configure", mm::build::shell_quote(tree.root() / "orphan"), log) == 65, "unregistered subtree is not a separate root");
    expect(invoke(bin / "configure", "--release", log) == 64, "unknown CLI option remains fatal");

    // Delete only the fixture's three known records: consumers must not need them.
    std::filesystem::remove(record_path, ec);
    std::filesystem::remove(tree.root() / "out-host/resolved-options.mdy", ec);
    std::filesystem::remove(tree.root() / "out-host/test/resolved-options.mdy", ec);
    expect(invoke(bin / "build", root_arg, log) == 0, "build accepts unknown manifest option with warning");
    expect(read_text(log).find("future-feature is ignored") != std::string::npos, "build warning identifies unknown name");
    tree.manifest_raw("test", "mm: 1.1\nkind: test\nname: example_test\nunit: app/main.cpp\noption: future-test no\nread-only: warnings\n");
    expect(invoke(bin / "test", mm::build::shell_quote(tree.root() / "test"), log) == 0, "test accepts 1.1 and retains execution");
    expect(read_text(log).find("future-test is ignored") != std::string::npos, "test warns on unsupported declaration");
    expect(invoke(bin / "check", root_arg, log) == 0, "check accepts 1.1 metadata");
    expect(invoke(bin / "model", root_arg, log) == 0, "model accepts 1.1 metadata");
    expect(invoke(bin / "mdy", "-h -o=" + mm::build::shell_quote(tree.root() / "site") + " " + mm::build::shell_quote(tree.root() / "mm.mdy"), log) == 0, "document renders 1.1 tree");
    expect(read_text(tree.root() / "site/app/index.html").find("future-feature") != std::string::npos, "rendered metadata is preserved");
    expect(!std::filesystem::exists(record_path), "consumers do not recreate records");

    tree.manifest("app", "kind: app\nname: example\nfile: main.cpp\nread-only: warnings\n");
    for (const auto& tool : {"build", "check", "model"})
        expect(invoke(bin / tool, root_arg, log) == 65, "shared gate rejects 1.1 keys in a 1.0 manifest");
    expect(invoke(bin / "mdy", "-h " + mm::build::shell_quote(tree.root() / "app/mm.mdy"), log) == 65, "direct manifest rendering uses shared gate");
}

const mm::test::case_ cases[] = {
    {"installed configure and cross-tool 1.1 compatibility", &installed_tools_support_11},
};
const mm::test::registrar reg{"mm.configure CLI", cases};
}
