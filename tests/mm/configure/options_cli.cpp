// Installed-tool integration: all effects are confined to a disposable project.
#include <chrono>
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
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: future-feature yes\noption: buildable-target no\nread-only: buildable-target\noption: core no\n");
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
    expect(read_text(log).find("core is ignored") == std::string::npos,
           "build handles core without an ignored warning");
    tree.manifest_raw("test", "mm: 1.1\nkind: test\nname: example_test\nunit: app/main.cpp\noption: future-test no\noption: buildable-target no\nread-only: buildable-target\noption: core no\n");
    expect(invoke(bin / "test", mm::build::shell_quote(tree.root() / "test"), log) == 0, "test accepts 1.1 and retains execution");
    expect(read_text(log).find("future-test is ignored") != std::string::npos, "test warns on unsupported declaration");
    expect(read_text(log).find("buildable-target is ignored") == std::string::npos,
           "test handles capability without an ignored warning");
    expect(read_text(log).find("core is ignored") == std::string::npos,
           "test handles core without an ignored warning");

    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: future-feature yes\noption: buildable-host no\n");
    expect(invoke(bin / "build", root_arg, log) == 0, "build skips a target unavailable in the host lane");
    expect(read_text(log).find("1 module/app target(s) skipped; not buildable-host") != std::string::npos,
           "build identifies the unavailable lane");
    tree.manifest_raw("test", "mm: 1.1\nkind: test\nname: example_test\nunit: app/main.cpp\noption: buildable-host no\n");
    expect(invoke(bin / "test", mm::build::shell_quote(tree.root() / "test"), log) ==
               mm::build::exit_unavailable,
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
    tree.manifest_raw("", "mm: 1.1\nkind: project\nname: lanes\nfolder: app\nfolder: test\nfolder: platforms\nfolder: library\n");
    tree.manifest_raw("app", "mm: 1.1\nkind: app\nname: example\nfile: main.cpp\noption: buildable-target no\n");
    tree.manifest("test", "kind: test\nname: example_test\nunit: app/main.cpp\n");
    tree.manifest_raw("platforms", "mm: 1.1\nkind: dir\nname: platforms\nfolder: aarch64\nfolder: m68k\n");
    tree.manifest_raw("platforms/aarch64",
                      "mm: 1.2\nkind: sdk\nname: aarch64-linux-glibc\n"
                      "target: aarch64-linux-gnu\ncompiler-family: gcc\nruntime: glibc\n"
                      "library: demo\n");
    tree.manifest_raw("platforms/m68k",
                      "mm: 1.2\nkind: sdk\nname: m68k-linux-glibc\n"
                      "target: m68k-linux-gnu\ncompiler-family: gcc\nruntime: glibc\n"
                      "runtime-prefix: " + tree.root().string() + "\n");
    tree.manifest_raw("library",
                      "mm: 1.2\nkind: library\nname: demo\nsource: third_party\n"
                      "licence: LICENSE\ninclude-directory: include\n");
    std::ofstream(tree.root() / "library/LICENSE") << "fixture licence\n";
    std::ofstream(tree.root() / "app/main.cpp")
        << "#include <iostream>\nint main(int argc, char** argv) { "
           "if (argc > 1) std::cout << argv[1] << '\\n'; return 0; }\n";
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
                  "--target aarch64-linux-gnu --sdk aarch64-linux-glibc "
                  "--compiler aarch64-linux-gnu-g++-16 --build release " +
                      root_arg,
                  log) == 65,
           "configure rejects an absent checkout selected by an SDK");
    expect(read_text(log).find("checkout is absent") != std::string::npos &&
               read_text(log).find("vendor.sh") == std::string::npos,
           "absent checkout invents no provisioning command when none exists");
    const auto vendor = tree.root() / "library/vendor.sh";
    std::ofstream(vendor) << "#!/bin/sh\nexit 0\n";
    std::filesystem::permissions(
        vendor,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, ec);
    expect(!ec, "fixture provisioning script is executable");
    expect(invoke(bin / "configure",
                  "--target aarch64-linux-gnu --sdk aarch64-linux-glibc "
                  "--compiler aarch64-linux-gnu-g++-16 --build release " +
                      root_arg,
                  log) == 65 && read_text(log).find("library/vendor.sh") != std::string::npos,
           "absent checkout names an existing executable provisioning script");
    std::filesystem::create_directories(tree.root() / "library/third_party/include");
    std::ofstream(tree.root() / "library/third_party/.checkout") << "present\n";
    expect(invoke(bin / "configure",
                  "--target aarch64-linux-gnu --sdk aarch64-linux-glibc "
                  "--compiler aarch64-linux-gnu-g++-16 --build release " +
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

    expect(invoke(bin / "configure",
                  "--target aarch64-linux-gnu --sdk aarch64-linux-glibc --target-host "
                  "--compiler aarch64-linux-gnu-g++-16 --build release " + root_arg,
                  log) == 0,
           "configure gives a hosted target host capability");
    const auto hosted_configuration = read_text(config);
    expect(hosted_configuration.find("target-host-capability: yes") != std::string::npos,
           "target host capability is persisted");
    expect(invoke_with_path(bin / "build", "--target " + root_arg, log, tree.root()) == 0,
           "hosted target builds a host-only application");
    expect(std::filesystem::exists(tree.root() /
                                   "out-target-aarch64-linux-gnu/app/example"),
           "host-only application is emitted in the hosted target lane");
    expect(invoke(bin / "configure", "--target-host " + root_arg, log) == 64,
           "target host capability requires an explicit target");

    expect(invoke(bin / "configure", "--host --compiler g++ --build debug " + root_arg,
                  log) == 0,
           "host reconfiguration retains the target lane");
    const auto host_configuration = read_text(config);
    expect(host_configuration.find("target-compiler: host") != std::string::npos &&
               host_configuration.find("cross-compiler: aarch64-linux-gnu-g++-16") !=
                   std::string::npos &&
               host_configuration.find("target-host-capability: yes") != std::string::npos,
           "host selection preserves the configured target and its capability");
    expect(invoke(bin / "configure", "--host --target aarch64-linux-gnu " + root_arg,
                  log) == 64,
           "configure rejects conflicting lane selectors");

    const auto m68k_driver = tree.root() / "m68k-linux-gnu-g++-16";
    std::ofstream(m68k_driver) << "#!/bin/sh\nexec g++ \"$@\"\n";
    const auto runner = tree.root() / "qemu-m68k";
    std::ofstream(runner) << "#!/bin/sh\nshift 2\n"
                             "if [ \"$1\" = -g ]; then shift 2; fi\n"
                             "exec \"$@\"\n";
    const auto debugger = tree.root() / "gdb-multiarch";
    std::ofstream(debugger) << "#!/bin/sh\nexit 0\n";
    for (const auto& program : {m68k_driver, runner, debugger})
        std::filesystem::permissions(
            program,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace, ec);
    expect(!ec, "fixture compiler and target runner are executable");
    expect(invoke_with_path(
               bin / "configure",
               "--target m68k-linux-gnu --target-host --compiler m68k-linux-gnu-g++-16 "
               "--sdk m68k-linux-glibc --runner qemu-user --debugger gdb --build release " + root_arg,
               log, tree.root()) == 0,
           "configure accepts a compatible named runner profile");
    expect(invoke_with_path(bin / "build", "--target " + root_arg, log, tree.root()) == 0,
           "runner fixture target application builds");
    expect(invoke_with_path(bin / "run", "--target " +
                                mm::build::shell_quote(tree.root() / "app") + " -- -h",
                            log, tree.root()) == 0,
           "run executes a target application through its configured runner");
    expect(read_text(log).find("-h") != std::string::npos,
           "run forwards application arguments after the separator");
    expect(invoke_with_path(bin / "debug", "--target " +
                                mm::build::shell_quote(tree.root() / "app"),
                            log, tree.root()) == 0,
           "debug connects a configured target debugger through its runner");
    expect(invoke(bin / "configure", "--target aarch64-linux-gnu --sdk aarch64-linux-glibc "
                                      "--runner qemu-user " +
                                      root_arg, log) == 64,
           "configure rejects an incompatible runner profile");
}

void installed_tools_support_common_help() {
    std::error_code ec;
    const auto bin = std::filesystem::current_path(ec) / "out/bin";
    expect(!ec, "installed tool directory available");

    for (const auto tool : {"build", "configure", "test", "check", "model", "run", "flash",
                            "debug", "shell", "sketch", "json"}) {
        const auto log = std::filesystem::temp_directory_path() /
                         (std::string("mm_help_") + tool + ".log");
        for (const auto flags : {"-h", "--help", "-v -h", "--verbose --help"}) {
            expect(invoke(bin / tool, flags, log) == 0,
                   std::string(tool) + " accepts " + flags);
            expect(read_text(log).starts_with(std::string("Usage: ") + tool),
                   std::string(tool) + " prints usage for " + flags);
        }
        std::filesystem::remove(log, ec);
    }
}

void sketch_tool_four_runs() {
    std::error_code ec;
    const auto repository = std::filesystem::current_path(ec);
    expect(!ec, "repository directory available");
    const auto bin = repository / "out/bin";
    const mm::test::scoped_tree tree{"sketch_cli"};

    // Scratch directory's parent manifest does not name the folder "scratch"
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: fixture\n");

    const auto scratch_dir = tree.root() / "scratch";
    std::filesystem::create_directories(scratch_dir, ec);
    expect(!ec, "scratch directory created");

    const auto log = tree.root() / "tool.log";
    const auto scratch_arg = mm::build::shell_quote(scratch_dir);

    // Write only scratch.ino
    const std::string ino_content = "void setup() {}\nvoid loop() {}\n";
    {
        std::ofstream ino_file(scratch_dir / "scratch.ino");
        ino_file << ino_content;
    }

    // Run 1: with only <name>.ino:
    // Generates both mm.mdy and main.cpp, prints "add folder: scratch" last, and exits 0
    expect(invoke(bin / "sketch", scratch_arg, log) == 0, "run 1: generating run on unregistered folder exits zero");
    const auto log1 = read_text(log);
    expect(log1.find("does not name scratch; add folder: scratch") != std::string::npos,
           "run 1: output contains add folder diagnostic");
    expect(std::filesystem::exists(scratch_dir / "mm.mdy"), "run 1: mm.mdy generated");
    expect(std::filesystem::exists(scratch_dir / "main.cpp"), "run 1: main.cpp generated");

    const auto main_content_first = read_text(scratch_dir / "main.cpp");
    const auto mdy_content_first = read_text(scratch_dir / "mm.mdy");

    // Run 2: again with manifest present:
    // Must regenerate main.cpp byte for byte and leave manifest alone
    expect(invoke(bin / "sketch", scratch_arg, log) == 0, "run 2: regenerates with manifest present");
    expect(read_text(scratch_dir / "main.cpp") == main_content_first, "run 2: main.cpp identical byte for byte");
    expect(read_text(scratch_dir / "mm.mdy") == mdy_content_first, "run 2: manifest untouched");

    // Run 3: with a second .ino the manifest does not name: must refuse
    {
        std::ofstream extra_ino(scratch_dir / "extra.ino");
        extra_ino << "void extra() {}\n";
    }
    expect(invoke(bin / "sketch", scratch_arg, log) == 65, "run 3: unmanifested .ino must be refused");
    expect(read_text(log).find("unmanifested .ino file") != std::string::npos, "run 3: names unmanifested file");
    std::filesystem::remove(scratch_dir / "extra.ino", ec);

    // Run 4: with parent manifest naming no such folder, sketch --check on it:
    // Must exit nonzero and print the "add folder:" diagnostic
    expect(invoke(bin / "sketch", "--check " + scratch_arg, log) != 0, "run 4: targeted check of unregistered folder fails");
    const auto log4 = read_text(log);
    expect(log4.find("does not name scratch; add folder: scratch") != std::string::npos,
           "run 4: check outputs add folder diagnostic");
}

// The json tool over the corpus fixtures: each command on a document the
// module accepts, a document it refuses with the line the message names, a
// missing file, and a missing command.
void installed_json_tool() {
    std::error_code ec;
    const auto bin = std::filesystem::current_path(ec) / "out/bin/json";
    const auto fixtures = std::filesystem::current_path(ec) / "tests/mm/json/fixtures";
    const auto log = std::filesystem::temp_directory_path() / "mm_json_tool.log";
    const auto accepted = mm::build::shell_quote(fixtures / "y_pass01.json");
    const auto refused = mm::build::shell_quote(fixtures / "n_array_extra_comma.json");
    const auto duplicate = mm::build::shell_quote(fixtures / "y_object_duplicated_key.json");

    expect(invoke(bin, "--check " + accepted, log) == 0 && read_text(log).empty(),
           "--check is silent and exits zero for an accepted document");
    expect(invoke(bin, "--check " + refused, log) == 1 &&
               read_text(log).find("n_array_extra_comma.json:1:") != std::string::npos &&
               read_text(log).find("(Malformed)") != std::string::npos,
           "--check names the file, line, and status for a refused document");
    expect(invoke(bin, "--check " + duplicate, log) == 1 &&
               read_text(log).find("(DuplicateKey)") != std::string::npos,
           "--check exits one for a document a policy rejects, naming the policy");
    expect(invoke(bin, "--check " + accepted + " " + refused, log) == 1,
           "--check over several files exits one when any is refused");
    expect(invoke(bin, "--indent " + accepted, log) == 0 && read_text(log).starts_with("[\n  "),
           "--indent writes the indented layout");
    expect(invoke(bin, "--compact " + accepted, log) == 0 && read_text(log).starts_with("[\"JSON"),
           "--compact writes the compact layout");
    expect(invoke(bin, "--scan " + accepted, log) == 0 && read_text(log).starts_with("0 0 array-begin"),
           "--scan prints one token per line from the first");
    expect(invoke(bin, "--scan " + refused, log) == 1 &&
               read_text(log).find("(Malformed)") != std::string::npos,
           "--scan stops at the scanner's fault with the same message");
    expect(invoke(bin, "--check " + mm::build::shell_quote(fixtures / "absent.json"), log) ==
                   mm::build::exit_manifest,
           "a file that cannot be read is exit_manifest");
    expect(invoke(bin, accepted, log) == mm::build::exit_usage,
           "a missing command is exit_usage");
    expect(invoke(bin, "--indent " + accepted + " " + refused, log) == mm::build::exit_usage,
           "--indent with two files is exit_usage");
    std::filesystem::remove(log, ec);
}

void installed_external_sketch() {
    std::error_code ec;
    const auto repository = std::filesystem::current_path(ec);
    expect(!ec, "repository directory available");
    const auto bin = repository / "out/bin";
    const mm::test::scoped_tree external_tree{"ext_sketch"};
    const auto sketch_dir = external_tree.root() / "fixture";
    std::filesystem::create_directories(sketch_dir, ec);
    expect(!ec, "external sketch directory created");

    const auto log = external_tree.root() / "tool.log";
    const auto sketch_arg = mm::build::shell_quote(sketch_dir);
    const auto repo_arg = mm::build::shell_quote(repository);

    // Write fixture.ino
    {
        std::ofstream ino_file(sketch_dir / "fixture.ino");
        ino_file << "#include <fstream>\n\n"
                 << "void setup() {\n"
                 << "    std::ifstream check(\"fixture.ino\");\n"
                 << "    if (check.is_open()) {\n"
                 << "        requestExit(0);\n"
                 << "    } else {\n"
                 << "        requestExit(1);\n"
                 << "    }\n"
                 << "}\n\n"
                 << "void loop() {}\n";
    }

    // 1. Generate external sketch with --project
    expect(invoke(bin / "sketch", "--project " + repo_arg + " " + sketch_arg,
                  log) == 0,
           "sketch generates external sketch with --project");
    const auto mdy_path = sketch_dir / "mm.mdy";
    expect(std::filesystem::exists(mdy_path), "mm.mdy was generated");
    const auto main_path = sketch_dir / "main.cpp";
    expect(std::filesystem::exists(main_path), "main.cpp was generated");
    const auto initial_mdy = read_text(mdy_path);
    expect(initial_mdy.find("project:") != std::string::npos,
           "generated manifest has project: key");
    expect(initial_mdy.find("sketch: fixture.ino") != std::string::npos,
           "generated manifest has sketch: key");

    // --project refused under a tree
    expect(invoke(bin / "sketch", "--project " + repo_arg + " " +
                      mm::build::shell_quote(repository / "apps/main"),
                  log) == 65,
           "sketch refuses --project when target is under a tree");

    // 2. Build via out/bin/build
    expect(invoke(bin / "build", sketch_arg, log) == 0,
           "build succeeds on external sketch");
    const auto exe = sketch_dir / "out-host/fixture";
    expect(std::filesystem::exists(exe),
           "executable placed under external root out-host");
    expect(!std::filesystem::exists(bin / "fixture"),
           "executable absent from project out/bin");

    // 3. Regeneration when main.cpp is deleted
    std::filesystem::remove(main_path, ec);
    expect(!std::filesystem::exists(main_path), "main.cpp removed");
    expect(invoke(bin / "build", sketch_arg, log) == 0,
           "build regenerates missing main.cpp");
    expect(std::filesystem::exists(main_path),
           "main.cpp regenerated during build");

    // 4. Regeneration when main.cpp is stale (older than fixture.ino)
    const auto old_time = std::filesystem::file_time_type::clock::now() -
                          std::chrono::seconds(10);
    std::filesystem::last_write_time(main_path, old_time, ec);
    const auto new_time = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(sketch_dir / "fixture.ino", new_time, ec);
    expect(invoke(bin / "build", sketch_arg, log) == 0,
           "build regenerates stale main.cpp");
    expect(std::filesystem::last_write_time(main_path, ec) >= new_time,
           "main.cpp updated during build");

    // 5. Run via out/bin/run --host (verifies cwd is sketch_dir)
    expect(invoke(bin / "run", "--host " + sketch_arg, log) == 0,
           "run --host verifies working directory is sketch_dir");

    // 6. Check via out/bin/check
    expect(invoke(bin / "check", sketch_arg, log) == 0,
           "check accepts valid external sketch");

    // Plant dialect violation in main.cpp
    {
        std::ofstream out(main_path, std::ios::app);
        out << "#define BAD_MACRO 1\n";
    }
    expect(invoke(bin / "check", sketch_arg, log) != 0,
           "check refuses dialect violation");

    // Regenerate to clean up main.cpp
    expect(invoke(bin / "sketch", sketch_arg, log) == 0,
           "sketch regenerates clean main.cpp");
    expect(invoke(bin / "check", sketch_arg, log) == 0,
           "check passes again after regeneration");

    // 7. Orphan refusal
    // Remove project: line from mm.mdy
    std::string no_project_mdy;
    {
        std::istringstream stream(initial_mdy);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.starts_with("project:")) {
                no_project_mdy += line + "\n";
            }
        }
        std::ofstream out(mdy_path);
        out << no_project_mdy;
    }
    expect(invoke(bin / "build", sketch_arg, log) == 65,
           "build refuses orphan external sketch without project:");

    expect(invoke(bin / "sketch", sketch_arg, log) == 65,
           "sketch refuses orphan external sketch on generation");
    const auto log_orphan = read_text(log);
    expect(log_orphan.find("neither registered by a parent nor external") !=
               std::string::npos,
           "sketch reports orphan diagnostic");

    expect(invoke(bin / "sketch", "--check " + sketch_arg, log) == 65,
           "sketch --check refuses orphan external sketch");
}

void installed_external_library_sketch() {
    std::error_code ec;
    const auto repository = std::filesystem::current_path(ec);
    expect(!ec, "repository directory available");
    const auto bin = repository / "out/bin";
    const mm::test::scoped_tree external_tree{"ext_lib_sketch"};
    const auto lib_root = external_tree.root() / "fixture_lib";
    const auto examples_dir = lib_root / "examples";
    const auto app_dir = examples_dir / "FixtureApp";
    std::filesystem::create_directories(app_dir, ec);
    expect(!ec, "library app directory created");

    const auto log = external_tree.root() / "tool.log";
    const auto lib_arg = mm::build::shell_quote(lib_root);
    const auto app_arg = mm::build::shell_quote(app_dir);
    const auto repo_arg = mm::build::shell_quote(repository);

    // 1. Write library.properties
    {
        std::ofstream props(lib_root / "library.properties");
        props << "name=FixtureLib\nversion=1.0.0\n";
    }

    // 2. Write FixtureLib.h using unqualified uint8_t and Arduino.h
    {
        std::ofstream header(lib_root / "FixtureLib.h");
        header << "#pragma once\n"
               << "#include \"Arduino.h\"\n"
               << "inline uint8_t fixture_add(uint8_t a, uint8_t b) {\n"
               << "    return a + b;\n"
               << "}\n";
    }

    // 3. Write FixtureLib.cpp
    {
        std::ofstream cpp(lib_root / "FixtureLib.cpp");
        cpp << "#include \"FixtureLib.h\"\n";
    }

    // 4. Write FixtureApp.ino
    {
        std::ofstream ino(app_dir / "FixtureApp.ino");
        ino << "#include \"FixtureLib.h\"\n\n"
            << "void setup() {\n"
            << "    uint8_t res = fixture_add(10, 20);\n"
            << "    if (res == 30) {\n"
            << "        requestExit(0);\n"
            << "    } else {\n"
            << "        requestExit(1);\n"
            << "    }\n"
            << "}\n\n"
            << "void loop() {}\n";
    }

    // 5. Generate library mode via sketch tool
    expect(invoke(bin / "sketch", "--project " + repo_arg + " " + lib_arg,
                  log) == 0,
           "sketch generates library manifests and files");
    expect(std::filesystem::exists(lib_root / "mm.mdy"),
           "root mm.mdy generated");
    expect(std::filesystem::exists(examples_dir / "mm.mdy"),
           "examples mm.mdy generated");
    expect(std::filesystem::exists(app_dir / "mm.mdy"),
           "app mm.mdy generated");
    expect(std::filesystem::exists(app_dir / "main.cpp"),
           "main.cpp generated");
    expect(std::filesystem::exists(app_dir / "Arduino.h"),
           "Arduino.h generated");

    // 6. Verify with --check
    expect(invoke(bin / "sketch", "--check " + lib_arg, log) == 0,
           "sketch --check passes on generated library");

    // 7. Compile and link via build (exercises uint8_t without std::)
    expect(invoke(bin / "build", app_arg, log) == 0,
           "build succeeds for library sketch application");

    // 8. Run executable
    expect(invoke(bin / "run", "--host " + app_arg, log) == 0,
           "run --host verifies sketch execution succeeds");

    // 9. Build-time repair: delete Arduino.h and verify build restores it
    const auto header_path = app_dir / "Arduino.h";
    std::filesystem::remove(header_path, ec);
    expect(!std::filesystem::exists(header_path),
           "Arduino.h removed for repair test");
    expect(invoke(bin / "build", app_arg, log) == 0,
           "build restores missing Arduino.h via check-generate-check");
    expect(std::filesystem::exists(header_path),
           "Arduino.h restored by build");
}

const mm::test::case_ cases[] = {
    {"installed configure and cross-tool 1.1 compatibility",
     &installed_tools_support_11},
    {"installed json tool", &installed_json_tool},
    {"installed tools select configured lanes",
     &installed_tools_select_configured_lanes},
    {"installed tools support common help",
     &installed_tools_support_common_help},
    {"sketch tool four-run case", &sketch_tool_four_runs},
    {"installed external sketch", &installed_external_sketch},
    {"installed external library sketch", &installed_external_library_sketch},
};
const mm::test::registrar reg{"mm.configure CLI", cases};
}
