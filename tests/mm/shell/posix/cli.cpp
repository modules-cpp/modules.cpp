// Native CLI and child-policy integration in disposable project trees.
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cerrno>
#include <iterator>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

import mm.shell.full;
import mm.shell.posix;
import mm.test;

namespace {

using mm::test::expect;

struct Result {
    int status = 127;
    std::string standard_output;
    std::string standard_error;
    std::string output;
};

void script(const std::filesystem::path& path,
            std::string_view source, bool executable = true);

[[nodiscard]] Result invoke(const std::filesystem::path& binary,
                            const std::filesystem::path& directory,
                            const std::vector<std::string>& arguments,
                            bool legacy = false,
                            std::string_view extra_path = {},
                            bool isolated_environment = false) {
    int output_pipe[2]{-1, -1};
    int error_pipe[2]{-1, -1};
    if (::pipe(output_pipe) != 0) return {};
    if (::pipe(error_pipe) != 0) {
        (void)::close(output_pipe[0]);
        (void)::close(output_pipe[1]);
        return {};
    }
    std::vector<std::string> environment;
    if (isolated_environment) {
        std::string path = "PATH=";
        if (!extra_path.empty()) {
            path.append(extra_path);
            path.push_back(':');
        }
        path.append("/usr/bin:/bin");
        environment = {path, "HOME=" + directory.string(),
                       "PWD=" + directory.string(), "LC_ALL=C"};
    }
    const auto child = ::fork();
    if (child < 0) {
        (void)::close(output_pipe[0]);
        (void)::close(output_pipe[1]);
        (void)::close(error_pipe[0]);
        (void)::close(error_pipe[1]);
        return {};
    }
    if (child == 0) {
        (void)::close(output_pipe[0]);
        (void)::close(error_pipe[0]);
        (void)::dup2(output_pipe[1], 1);
        (void)::dup2(error_pipe[1], 2);
        (void)::close(output_pipe[1]);
        (void)::close(error_pipe[1]);
        if (::chdir(directory.c_str()) != 0) ::_exit(127);
        if (legacy) {
            (void)::setenv("MM_SHELL_LEGACY", "1", 1);
        } else {
            (void)::unsetenv("MM_SHELL_LEGACY");
        }
        if (!isolated_environment && !extra_path.empty()) {
            const auto* inherited = ::getenv("PATH");
            std::string path{extra_path};
            if (inherited != nullptr) {
                path.push_back(':');
                path.append(inherited);
            }
            (void)::setenv("PATH", path.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        auto name = binary.string();
        argv.push_back(name.data());
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        if (isolated_environment) {
            std::vector<char*> envp;
            for (auto& entry : environment) {
                envp.push_back(entry.data());
            }
            envp.push_back(nullptr);
            (void)::execve(binary.c_str(), argv.data(), envp.data());
        } else {
            (void)::execv(binary.c_str(), argv.data());
        }
        ::_exit(127);
    }
    (void)::close(output_pipe[1]);
    (void)::close(error_pipe[1]);
    Result result;
    struct pollfd streams[2]{
        {output_pipe[0], POLLIN, 0},
        {error_pipe[0], POLLIN, 0},
    };
    std::size_t open = 2;
    char bytes[1024];
    while (open != 0) {
        if (::poll(streams, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (std::size_t i = 0; i < 2; ++i) {
            if (streams[i].fd < 0 ||
                (streams[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            const auto count = ::read(streams[i].fd, bytes,
                                      sizeof(bytes));
            if (count > 0) {
                auto& target = i == 0 ? result.standard_output
                                      : result.standard_error;
                target.append(bytes, static_cast<std::size_t>(count));
            } else if (count == 0 || errno != EINTR) {
                (void)::close(streams[i].fd);
                streams[i].fd = -1;
                --open;
            }
        }
    }
    for (auto& stream : streams) {
        if (stream.fd >= 0) (void)::close(stream.fd);
    }
    result.output = result.standard_output + result.standard_error;
    int raw = 0;
    if (::waitpid(child, &raw, 0) >= 0 && WIFEXITED(raw)) {
        result.status = WEXITSTATUS(raw);
    }
    return result;
}

void wrapper_contracts() {
    const auto repository = std::filesystem::current_path();
    const mm::test::scoped_tree installed{"shell_wrappers_installed"};
    const mm::test::scoped_tree missing{"shell_wrappers_missing"};
    std::filesystem::create_directories(installed.root() / "out/bin");
    script(installed.root() / "out/bin/shell",
           "#!/bin/sh\nprintf 'arg:%s\\n' \"$@\"\n");
    struct Wrapper {
        std::string_view name;
        std::string_view banner;
    };
    constexpr Wrapper wrappers[]{
        {"bootstrap", "Run bootstrap\n"},
        {"build", "Run build\n"},
        {"check", "Run check\n"},
        {"clean", "Run clean\n"},
        {"configure", "Run configure\n"},
        {"debug", "Debug application\n"},
        {"document", "Run document\n"},
        {"flash", "Flash application\n"},
        {"json", ""},
        {"model", "Run model\n"},
        {"run", "Run application\n"},
        {"sketch", "Run sketch\n"},
        {"test", "Run test\n"},
    };
    for (const auto& wrapper : wrappers) {
        const auto name = std::string(wrapper.name);
        std::filesystem::copy_file(repository / name,
                                   installed.root() / name);
        std::filesystem::copy_file(repository / name,
                                   missing.root() / name);
        script(missing.root() / (name + ".sh"),
               "#!/bin/sh\nprintf 'fallback:%s\\n' \"$@\"\n");
        const auto normal = invoke(
            "/bin/sh", installed.root(),
            {name, "two words", "quote'argument"});
        const auto marker = "arg:--run\narg:" + name +
            ".sh\narg:--\narg:two words\narg:quote'argument\n";
        expect(normal.status == 0 &&
                   normal.output.find(marker) != std::string::npos &&
                   normal.output.starts_with(wrapper.banner),
               "installed wrapper passes argv directly and keeps banner");
        const auto rollback = invoke(
            "/bin/sh", installed.root(), {name, "value"}, true);
        expect(rollback.status == 0 &&
                   rollback.output.find("arg:--legacy-sh\n") !=
                       std::string::npos,
               "legacy flag is an explicit one-release escape");
        const auto fallback = invoke(
            "/bin/sh", missing.root(), {name, "two words"});
        expect(fallback.status == 0 &&
                   fallback.output.find("fallback:two words\n") !=
                       std::string::npos &&
                   fallback.output.starts_with(wrapper.banner),
               "missing-tool fallback remains usable");
    }
}

void script(const std::filesystem::path& path,
            std::string_view source, bool executable) {
    std::ofstream{path, std::ios::binary} << source;
    if (!executable) return;
    std::filesystem::permissions(
        path, std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::add);
}

[[nodiscard]] std::filesystem::path shell_binary() {
    return std::filesystem::current_path() / "out/bin/shell";
}

void cli_modes_and_exit_codes() {
    const auto binary = shell_binary();
    expect(std::filesystem::exists(binary), "installed shell exists");
    const mm::test::scoped_tree tree{"shell_cli_modes"};
    tree.manifest("", "kind: project\nname: shell_cli_modes\n");
    script(tree.root() / "sample.sh", "#!/bin/sh\necho run \"$1\"\n");
    const auto run = [&](const std::vector<std::string>& args) {
        return invoke(binary, tree.root(), args);
    };

    const auto command = run({"-c", "echo native $((2+3))", "--"});
    expect(command.status == 0 && command.output == "native 5\n",
           "-c runs native arithmetic and output");
    const auto positional = run({"-c", "echo \"$@\"", "--",
                                 "alpha", "beta"});
    expect(positional.status == 0 &&
               positional.output == "alpha beta\n",
           "-c preserves positional values after --");
    const auto no_positionals = run({
        "-c", "set -- \"$@\"; echo $#", "--"});
    expect(no_positionals.status == 0 && no_positionals.output == "0\n",
           "quoted $@ contributes no field without positional arguments");
    const auto substitution = run({
        "-c", "status=0; value=$(exit 64) || status=$?; echo $status",
        "--"});
    expect(substitution.status == 0 && substitution.output == "64\n",
           "assignment-only command keeps substitution exit status");
    const auto canonical = run({
        "-c", "echo \"$(CDPATH= cd -- . && pwd)\""});
    expect(canonical.status == 0 &&
               canonical.output == tree.root().string() + "\n",
           "cd -- in command substitution publishes canonical pwd");
    const auto files = run({
        "-c", "[ -x sample.sh ] && [ -f sample.sh ] && "
              "[ -d . ] && [ -s sample.sh ] && echo files"});
    expect(files.status == 0 && files.output == "files\n",
           "file predicates consult the host file-service boundary");
    const auto file = run({"--run", "sample.sh", "--", "value"});
    expect(file.status == 0 && file.output == "run value\n",
           "--run executes a project script natively");
    expect(run({"--check", "sample.sh"}).status == 0,
           "--check accepts a valid script");
    expect(run({"--tokens", "sample.sh"}).output.find("word") !=
               std::string::npos,
           "--tokens reports lexical tokens");
    expect(run({"--dump-ast", "sample.sh"}).output.find("program") !=
               std::string::npos,
           "--dump-ast reports syntax nodes");
    expect(run({"--capabilities"}).output.find("scripts") !=
               std::string::npos,
           "--capabilities reports the full profile");
    expect(run({"--commands"}).output.find("echo") !=
               std::string::npos,
           "--commands lists registered builtins");
    const auto embedded = run({"--profile=embedded", "-c",
                               "echo embedded $((6*7))"});
    expect(embedded.status == 0 &&
               embedded.output == "embedded 42\n",
           "embedded mode uses the bounded evaluator");
    expect(run({"-c", "exit 7"}).status == 7,
           "script exit status reaches the caller");
    const auto exported = run({"-e", "SHELL_FIXTURE=kept", "-c",
                               "echo \"$SHELL_FIXTURE\""});
    expect(exported.status == 0 && exported.output == "kept\n",
           "-e initializes an exported variable");
    expect(run({"--run"}).status == 64,
           "missing file is a usage error");
    const auto pipeline = run({
        "-c",
        "awk 'BEGIN {for (i=0; i<100000; ++i) print i}' | wc -l"});
    expect(pipeline.status == 0 &&
               pipeline.output.find("100000") != std::string::npos,
           "a producer larger than pipe capacity runs concurrently");
    const auto legacy = run({"--legacy-sh", "-c", "echo legacy"});
    expect(legacy.status == 0 &&
               legacy.output.find("legacy\n") != std::string::npos &&
               legacy.output.find("deprecated") != std::string::npos,
           "legacy mode is explicit and diagnosed");
}

void native_child_resolution() {
    const auto binary = shell_binary();
    const mm::test::scoped_tree tree{"shell_native_children"};
    const mm::test::scoped_tree outside{"shell_native_outside"};
    tree.manifest("", "kind: project\nname: native_children\n");
    script(tree.root() / "parent.sh",
           "#!/bin/sh\n./child.sh \"$1\"\n");
    script(tree.root() / "child.sh",
           "#!/bin/sh\necho child \"$1\"\n");
    script(tree.root() / "plain.sh", "echo plain\n");
    script(tree.root() / "nonexec.sh",
           "#!/bin/sh\necho hidden\n", false);
    script(tree.root() / "foreign.sh",
           "#!/bin/bash\necho foreign\n");
    script(tree.root() / "status.sh", "#!/bin/sh\nexit 9\n");
    script(tree.root() / "build", "#!/bin/sh\nexit 17\n");
    script(outside.root() / "escape.sh",
           "#!/bin/sh\necho escaped\n");
    std::filesystem::create_symlink(
        outside.root() / "escape.sh", tree.root() / "escape.sh");

    const auto run = [&](std::string_view command) {
        return invoke(binary, tree.root(),
                      {"-c", std::string(command), "--project",
                       (tree.root() / "mm.mdy").string()});
    };
    const auto nested = invoke(binary, tree.root(),
                               {"--run", "parent.sh", "--", "value"});
    expect(nested.status == 0 && nested.output == "child value\n",
           "nested project script preserves arguments");
    const auto plain = run("./plain.sh");
    expect(plain.status == 126 &&
               plain.output.find("plain\n") == std::string::npos,
           "executable text without a shebang never falls to /bin/sh");
    expect(run("./nonexec.sh").status == 126,
           "non-executable script is refused");
    expect(run("./foreign.sh").output == "foreign\n",
           "foreign shebang keeps external exec behavior");
    const auto escape = run("./escape.sh");
    expect(escape.status == 126 &&
               escape.output.find("escaped\n") ==
                   std::string::npos,
           "symlink outside project cannot enter native policy");

    mm::shell::posix::HostServices host;
    const auto directory = tree.root().string();
    expect(host.set_native_project_root(directory),
           "host service records the canonical project root");
    const std::string_view arguments[]{"./status.sh"};
    mm::shell::full::ProcessRequest request;
    request.arguments = arguments;
    request.directory = directory;
    auto process = host.process();
    mm::shell::full::Handle child =
        mm::shell::full::invalid_handle;
    expect(process.spawn(process.context, request, child) ==
               mm::shell::full::ServiceStatus::Ok &&
               host.native_scripts_started() == 1,
           "project script enters the native interpreter before exec");
    int status = 0;
    expect(process.wait(process.context, child, status) ==
               mm::shell::full::ServiceStatus::Ok && status == 9,
           "native child exit reaches the process-service caller");
    const std::string_view wrapper[]{"./build"};
    request.arguments = wrapper;
    expect(process.spawn(process.context, request, child) ==
               mm::shell::full::ServiceStatus::Ok &&
               host.native_scripts_started() == 2,
           "registered no-extension wrapper enters native policy");
    expect(process.wait(process.context, child, status) ==
               mm::shell::full::ServiceStatus::Ok && status == 17,
           "registered wrapper returns its native status");
}

void safe_script_dash_differential() {
    if (!std::filesystem::exists("/bin/dash")) return;
    const auto binary = shell_binary();
    const auto repository = std::filesystem::current_path();
    const mm::test::scoped_tree tree{"shell_dash_help"};
    tree.manifest("", "kind: project\nname: dash_help\n");
    std::filesystem::create_directories(tree.root() / "scripts");
    constexpr std::string_view names[]{
        "build-analog-smoke-pico.sh",
        "build-blink-linux.sh",
        "build-blink-pico.sh",
        "build-font-demo-pico-epaper.sh",
        "build-font-demo-rp2350_touch_lcd_28.sh",
        "build-gfx-demo-pico-epaper.sh",
        "build-gfx-demo-rp2350_touch_lcd_28.sh",
        "build-gpio-edge-smoke-pico.sh",
        "build-linux-board-smoke.sh",
        "build-linux-display-demo.sh",
        "build-linux-epaper-font-demo.sh",
        "build-linux-epaper-gfx-demo.sh",
        "build-linux-sdl-font-demo.sh",
        "build-linux-sdl-gfx-demo.sh",
        "build-linux-sdl.sh",
        "build-linux-smoke.sh",
        "build-pico.sh",
        "build-stdio-smoke-pico-sdk.sh",
        "configure-pico.sh",
        "release.sh",
    };
    for (const auto name : names) {
        const auto relative = std::string{"scripts/"} +
                              std::string{name};
        std::filesystem::copy_file(repository / relative,
                                   tree.root() / relative);
        const auto dash = invoke("/bin/dash", tree.root(),
                                 {relative, "--help"}, false, {}, true);
        const auto native = invoke(binary, tree.root(),
                                   {"--run", relative, "--", "--help"},
                                   false, {}, true);
        if (native.status != dash.status ||
            native.standard_output != dash.standard_output ||
            native.standard_error != dash.standard_error) {
            std::cerr << relative << " dash=" << dash.status
                      << " native=" << native.status << "\n"
                      << "dash: " << dash.output << "\n"
                      << "native: " << native.output << "\n";
        }
        expect(native.status == dash.status &&
                   native.standard_output == dash.standard_output &&
                   native.standard_error == dash.standard_error,
               "safe tracked script help matches dash exactly");
    }
}

void fixture_script_dash_differential() {
    if (!std::filesystem::exists("/bin/dash")) return;
    const auto binary = shell_binary();
    const auto repository = std::filesystem::current_path();
    struct Case {
        std::string_view path;
        std::string_view argument;
    };
    constexpr Case cases[]{
        {"bootstrap.sh", ""},
        {"build.sh", ""},
        {"check.sh", ""},
        {"clean.sh", ""},
        {"configure.sh", ""},
        {"debug.sh", ""},
        {"document.sh", "--help"},
        {"flash.sh", ""},
        {"json.sh", ""},
        {"model.sh", ""},
        {"run.sh", ""},
        {"sketch.sh", ""},
        {"test.sh", "--not-supported"},
        {"scripts/build-board-smoke-rp2350_touch_lcd_28.sh", ""},
        {"platforms/pico/install-sdk-tools.sh", ""},
        {"platforms/pico/sdk/pico-sdk/vendor.sh", ""},
    };
    struct Observation {
        Result process;
        std::vector<std::string> files;
    };
    const auto execute = [&](const Case& item, bool native) {
        const mm::test::scoped_tree tree{"shell_dash_fixture"};
        tree.manifest("", "kind: project\nname: dash_fixture\n");
        const auto relative = std::filesystem::path{item.path};
        std::filesystem::create_directories(
            (tree.root() / relative).parent_path());
        std::filesystem::copy_file(repository / relative,
                                   tree.root() / relative);
        std::filesystem::create_directories(tree.root() / "out/bin");
        std::filesystem::create_directories(tree.root() / "bin");
        for (const auto tool : {"build", "check", "configure", "debug",
                                "flash", "json", "model", "run",
                                "sketch"}) {
            script(tree.root() / "out/bin" / tool,
                   "#!/bin/sh\n"
                   "{ printf 'program:%s\\n' \"$0\"; "
                   "printf 'cwd:%s\\n' \"$(pwd -P)\"; "
                   "printf 'arg:%s\\n' \"$@\"; "
                   "env | LC_ALL=C sort; } >> fixture.trace\n"
                   "printf 'tool:%s\\n' \"$0\"\n");
        }
        script(tree.root() / "bin/cppcheck", "#!/bin/sh\nexit 0\n");
        script(tree.root() / "bin/c++",
               "#!/bin/sh\n"
               "{ printf 'program:%s\\n' \"$0\"; "
               "printf 'cwd:%s\\n' \"$(pwd -P)\"; "
               "printf 'arg:%s\\n' \"$@\"; "
               "env | LC_ALL=C sort; } >> fixture.trace\n"
               "case \"$1\" in\n"
               "  --version) echo 'c++ fake 15'; exit 0 ;;\n"
               "  -dumpversion) echo 15; exit 0 ;;\n"
               "esac\necho 'fake compiler refused' >&2\nexit 1\n");
        if (relative ==
            "platforms/pico/install-sdk-tools.sh") {
            const auto target = tree.root() /
                "platforms/pico/pico-sdk";
            std::filesystem::create_directories(target);
            std::ofstream{target / "marker"} << "occupied\n";
        }
        if (relative ==
            "platforms/pico/sdk/pico-sdk/vendor.sh") {
            const auto target = tree.root() /
                "platforms/pico/sdk/pico-sdk/upstream";
            std::filesystem::create_directories(target);
            std::ofstream{target / "marker"} << "occupied\n";
        }
        std::vector<std::string> args;
        if (native) {
            args = {"--run", relative.string(), "--"};
        } else {
            args = {relative.string()};
        }
        if (!item.argument.empty()) {
            args.emplace_back(item.argument);
        }
        Observation observed;
        observed.process = invoke(native ? binary :
                                  std::filesystem::path{"/bin/dash"},
                                  tree.root(), args, false,
                                  (tree.root() / "bin").string(), true);
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(tree.root())) {
            const auto path = std::filesystem::relative(
                entry.path(), tree.root()).string();
            if (entry.is_directory()) {
                observed.files.push_back("dir:" + path);
            } else if (entry.is_regular_file()) {
                std::ifstream input{entry.path(), std::ios::binary};
                const std::string contents{
                    std::istreambuf_iterator<char>{input},
                    std::istreambuf_iterator<char>{}};
                observed.files.push_back("file:" + path + "=" +
                                         contents);
            }
        }
        std::sort(observed.files.begin(), observed.files.end());
        return observed;
    };
    for (const auto& item : cases) {
        const auto dash = execute(item, false);
        const auto native = execute(item, true);
        if (dash.process.status != native.process.status ||
            dash.process.standard_output !=
                native.process.standard_output ||
            dash.process.standard_error !=
                native.process.standard_error ||
            dash.files != native.files) {
            std::cerr << item.path << " dash=" << dash.process.status
                      << " native=" << native.process.status << "\n"
                      << "dash: " << dash.process.output << "\n"
                      << "native: " << native.process.output << "\n";
            if (dash.files != native.files) {
                const auto count = std::min(dash.files.size(),
                                            native.files.size());
                for (std::size_t i = 0; i < count; ++i) {
                    if (dash.files[i] == native.files[i]) continue;
                    std::cerr << "dash file: " << dash.files[i] << "\n"
                              << "native file: " << native.files[i]
                              << "\n";
                    break;
                }
                std::cerr << "file counts: " << dash.files.size()
                          << " vs " << native.files.size() << "\n";
            }
        }
        expect(dash.process.status == native.process.status &&
                   dash.process.standard_output ==
                       native.process.standard_output &&
                   dash.process.standard_error ==
                       native.process.standard_error &&
                   dash.files == native.files,
               "isolated tracked script matches dash and fixture files");
    }
}

const mm::test::case_ cases[]{
    {"native CLI modes and exit codes", &cli_modes_and_exit_codes},
    {"native child resolution", &native_child_resolution},
    {"all root wrapper contracts", &wrapper_contracts},
    {"safe tracked script differential", &safe_script_dash_differential},
    {"isolated tracked script differential",
     &fixture_script_dash_differential},
};

const mm::test::registrar reg{"mm.shell.posix CLI", cases};

}  // namespace
