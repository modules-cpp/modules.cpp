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

int invoke_with_path(const std::filesystem::path& binary, std::string_view arguments,
                     const std::filesystem::path& log,
                     const std::filesystem::path& path) {
    return mm::build::run(mm::build::default_toolchain(),
                          "PATH=" + mm::build::shell_quote(path) + ":\"$PATH\" " +
                              mm::build::shell_quote(binary) + " " + std::string(arguments) +
                              " > " + mm::build::shell_quote(log) + " 2>&1");
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
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: future-feature yes\noption: buildable-target no\nread-only: buildable-target\n");
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
    expect(read_text(log).find("buildable-target is ignored") == std::string::npos,
           "build handles capability without an ignored warning");
    tree.manifest_raw("test", "mm: 1.1\nkind: test\nname: example_test\nunit: app/main.cpp\noption: future-test no\noption: buildable-target no\nread-only: buildable-target\n");
    expect(invoke(bin / "test", mm::build::shell_quote(tree.root() / "test"), log) == 0, "test accepts 1.1 and retains execution");
    expect(read_text(log).find("future-test is ignored") != std::string::npos, "test warns on unsupported declaration");
    expect(read_text(log).find("buildable-target is ignored") == std::string::npos,
           "test handles capability without an ignored warning");

    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: future-feature yes\noption: buildable-host no\n");
    expect(invoke(bin / "build", root_arg, log) == 0, "build skips a target unavailable in the host lane");
    expect(read_text(log).find("1 module/app target(s) skipped; not buildable-host") != std::string::npos,
           "build identifies the unavailable lane");
    tree.manifest_raw("test", "mm: 1.1\nkind: test\nname: example_test\nunit: app/main.cpp\noption: buildable-host no\n");
    expect(invoke(bin / "test", mm::build::shell_quote(tree.root() / "test"), log) == 65,
           "test rejects a target unavailable in the host lane");
    expect(read_text(log).find("example_test is not buildable-host") != std::string::npos,
           "test identifies the unavailable test and lane");
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

void installed_tools_select_configured_lanes() {
    std::error_code ec;
    const auto repository = std::filesystem::current_path(ec);
    expect(!ec, "repository directory available");
    const auto bin = repository / "out/bin";
    const mm::test::scoped_tree tree{"lane_cli"};
    tree.manifest_raw("", "mm: 1.1\nkind: project\nname: lanes\nfolder: app\nfolder: test\n");
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: buildable-target no\n");
    tree.manifest("test", "kind: test\nname: example_test\nunit: app/main.cpp\n");
    std::ofstream(tree.root() / "app/main.cpp") << "int main() { return 0; }\n";
    const auto driver = tree.root() / "aarch64-linux-gnu-g++-16";
    std::ofstream(driver) << "#!/bin/sh\nexec g++ \"$@\"\n";
    std::filesystem::permissions(
        driver,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, ec);
    expect(!ec, "fixture target compiler shim is executable");

    const auto root_arg = mm::build::shell_quote(tree.root());
    const auto test_arg = mm::build::shell_quote(tree.root() / "test");
    const auto log = tree.root() / "lane.log";
    const auto config = tree.root() / "out/config.mdy";

    expect(invoke(bin / "configure", "--host --compiler g++ --build release " + root_arg,
                  log) == 0,
           "configure writes and selects the host lane");
    expect(invoke(bin / "build", "--target " + root_arg, log) == 65,
           "build rejects a missing target lane");
    expect(read_text(log).find("target lane is not configured") != std::string::npos,
           "missing target lane is diagnosed");

    expect(invoke(bin / "configure",
                  "--target aarch64-linux-gnu --compiler aarch64-linux-gnu-g++-16 --build release " +
                      root_arg,
                  log) == 0,
           "configure writes and selects a target lane");
    const auto target_configuration = read_text(config);
    expect(target_configuration.find("host-compiler: g++") != std::string::npos &&
               target_configuration.find("cross-compiler: aarch64-linux-gnu-g++-16") !=
                   std::string::npos &&
               target_configuration.find("target-build-directory: out-target-aarch64-linux-gnu") !=
                   std::string::npos,
           "target configuration retains the host and isolates target output");

    expect(invoke(bin / "build", "--target " + root_arg, log) == 0,
           "target build skips a host-only application without invoking its compiler");
    expect(read_text(log).find("not buildable-target") != std::string::npos,
           "target build reports the capability skip");
    expect(read_text(config) == target_configuration,
           "per-invocation target selection does not rewrite configuration");

    expect(invoke(bin / "build", "--host " + root_arg, log) == 0,
           "host build overrides the persisted target selection");
    expect(std::filesystem::exists(tree.root() / "out-host/app/example"),
           "host build writes the host executable");
    expect(read_text(config) == target_configuration,
           "per-invocation host selection does not rewrite configuration");
    expect(invoke(bin / "build", "--host --target " + root_arg, log) == 64,
           "build rejects conflicting lane selectors");

    expect(invoke(bin / "test", "--target " + test_arg, log) == 65,
           "target test without a runner fails safely");
    expect(read_text(log).find("has no test runner; use --compile-only") != std::string::npos,
           "missing target runner is diagnosed");
    expect(invoke_with_path(bin / "test", "--target --compile-only " + test_arg, log,
                            tree.root()) == 0,
           "target compile-only test compiles and links without execution");
    expect(std::filesystem::exists(tree.root() /
                                   "out-target-aarch64-linux-gnu/tests/example_test/example_test"),
           "target test binary stays in the target lane");
    expect(read_text(log).find("\nRun\n") == std::string::npos,
           "target compile-only test does not run the binary");
    expect(invoke(bin / "test", "--host --compile-only " + test_arg, log) == 0,
           "host compile-only test compiles and links");
    expect(read_text(log).find("\nRun\n") == std::string::npos,
           "compile-only test does not run the binary");
    expect(invoke(bin / "test", "--host " + test_arg, log) == 0,
           "host test runs while target remains the persisted default");
    expect(invoke(bin / "test", "--host --target " + test_arg, log) == 64,
           "test rejects conflicting lane selectors");

    expect(invoke(bin / "configure", "--host --compiler g++ --build debug " + root_arg,
                  log) == 0,
           "host reconfiguration retains the target lane");
    const auto host_configuration = read_text(config);
    expect(host_configuration.find("target-compiler: host") != std::string::npos &&
               host_configuration.find("cross-compiler: aarch64-linux-gnu-g++-16") !=
                   std::string::npos,
           "host selection preserves the configured target");
    expect(invoke(bin / "configure", "--host --target aarch64-linux-gnu " + root_arg,
                  log) == 64,
           "configure rejects conflicting lane selectors");
}

const mm::test::case_ cases[] = {
    {"installed configure and cross-tool 1.1 compatibility", &installed_tools_support_11},
    {"installed tools select configured lanes", &installed_tools_select_configured_lanes},
};
const mm::test::registrar reg{"mm.configure CLI", cases};
}
