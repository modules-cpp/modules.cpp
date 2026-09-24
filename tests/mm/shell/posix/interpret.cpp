// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.shell;
import mm.shell.full;
import mm.shell.posix;
import mm.test;

namespace {

using mm::shell::full::FullState;
using mm::shell::full::Interpreter;
using mm::shell::full::ServiceStatus;
using mm::shell::posix::HostServices;
using mm::test::expect;
using mm::test::scoped_tree;

[[nodiscard]] std::string read_file(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file},
                       std::istreambuf_iterator<char>{}};
}

void write_file(const std::string& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary};
    file << text;
}

// Runs a script with real host services and returns whatever it wrote to the
// captured ordinary-output file.
struct Run {
    scoped_tree tree;
    HostServices host;
    FullState state;
    std::string output_path;
    int status = 0;
    ServiceStatus service = ServiceStatus::Ok;
    std::string diagnostic;
    std::size_t spawns = 0;
    std::string output;

    explicit Run(std::string_view name) : tree{name} {
        output_path = (tree.root() / "captured.out").string();
        state.set_directory(tree.root().string());
    }

    void go(std::string_view source,
            std::span<const std::string_view> arguments = {}) {
        mm::shell::full::Handle sink = mm::shell::full::invalid_handle;
        const auto io = host.io();
        expect(io.open(io.context, output_path,
                       mm::shell::full::OpenMode::Truncate, sink) ==
                   ServiceStatus::Ok,
               "capture file opens");
        Interpreter interpreter{state, host.all()};
        interpreter.set_streams({mm::shell::full::invalid_handle, sink,
                                 sink});
        expect(interpreter.set_arguments("script", arguments) ==
                   mm::shell::Status::Ok,
               "arguments install");
        const auto outcome = interpreter.run_text(source);
        status = outcome.status;
        service = outcome.service;
        diagnostic = outcome.diagnostic.message;
        spawns = interpreter.spawn_count();
        expect(io.close(io.context, sink) == ServiceStatus::Ok,
               "capture file closes");
        output = read_file(output_path);
    }
};

void runs_a_script_end_to_end() {
    Run run{"interpret_script"};
    const std::string_view arguments[]{"alpha", "beta"};
    run.go(
        "count=0\n"
        "greet() {\n"
        "    echo greet $1\n"
        "}\n"
        "if [ -n \"$count\" ]; then\n"
        "    greet one\n"
        "fi\n"
        "for item in a b; do\n"
        "    count=$((count + 1))\n"
        "    echo item $item\n"
        "done\n"
        "while [ \"$count\" -lt 4 ]; do\n"
        "    count=$((count + 1))\n"
        "done\n"
        "case $1 in\n"
        "    al*) echo matched $1 ;;\n"
        "    *) echo other ;;\n"
        "esac\n"
        "echo args $# \"$2\"\n"
        "echo count $count\n",
        arguments);
    expect(run.status == 0 && run.service == ServiceStatus::Ok,
           "the script completes successfully");
    expect(run.output ==
               "greet one\n"
               "item a\n"
               "item b\n"
               "matched alpha\n"
               "args 2 beta\n"
               "count 4\n",
           "every construct produced its expected line");
    expect(run.spawns == 0,
           "no construct in that script reached the process service");
}

void redirections_and_here_documents() {
    Run run{"interpret_redirect"};
    const auto target = (run.tree.root() / "written.txt").string();
    run.go("name=world\n"
           "echo first > " + target + "\n"
           "echo second >> " + target + "\n"
           "cat <<PLAN >> " + target + "\n"
           "hello $name\n"
           "PLAN\n"
           "cat <<'RAW' >> " + target + "\n"
           "$name stays\n"
           "RAW\n"
           "echo done\n");
    expect(run.status == 0, "the redirection script completes");
    expect(run.output == "done\n",
           "redirected output did not reach the shell's own stream");
    expect(read_file(target) ==
               "first\nsecond\nhello world\n$name stays\n",
           "files, appends, and both here-document forms landed in order");
    expect(run.spawns == 2,
           "only the two cat commands reached the process service");
}

void pipelines_and_externals() {
    Run run{"interpret_pipeline"};
    run.go("echo one two three | tr ' ' '\\n' | grep t\n"
           "echo status $?\n"
           "mm-absent-program\n"
           "echo after $?\n");
    expect(run.output ==
               "two\nthree\nstatus 0\n"
               "mm-absent-program: not found\n"
               "after 127\n",
           "a builtin feeds externals and a missing program is 127 on stderr");
    expect(run.spawns == 2, "the two external stages spawned once each");
}

void command_substitution_and_state_isolation() {
    Run run{"interpret_substitution"};
    run.go("outer=parent\n"
           "value=$(echo captured; outer=child)\n"
           "echo value $value\n"
           "echo outer $outer\n"
           "echo nested $(echo $(echo deep))\n"
           // dash expands $? before the substitution's status lands, so the
           // old value is what a later word in the same command sees.
           "echo status $(exit 5) $?\n");
    expect(run.status == 0, "the substitution script completes");
    expect(run.output ==
               "value captured\n"
               "outer parent\n"
               "nested deep\n"
               "status 0\n",
           "captures land, a child cannot mutate its parent, nesting works");
}

void functions_subshells_and_flow() {
    Run run{"interpret_flow"};
    run.go("shared=start\n"
           "body() {\n"
           "    echo in $1\n"
           "    shared=changed\n"
           "    return 3\n"
           "}\n"
           "body one\n"
           "echo status $?\n"
           "echo shared $shared\n"
           "( shared=subshell; echo inner $shared )\n"
           "echo outer $shared\n"
           "for i in a b c; do\n"
           "    if [ $i = b ]; then continue; fi\n"
           "    if [ $i = c ]; then break; fi\n"
           "    echo loop $i\n"
           "done\n"
           "echo end\n");
    expect(run.output ==
               "in one\n"
               "status 3\n"
               "shared changed\n"
               "inner subshell\n"
               "outer changed\n"
               "loop a\n"
               "end\n",
           "a function shares state, a subshell does not, flow words work");
}

void errexit_traps_and_exit() {
    Run failing{"interpret_errexit"};
    failing.go("set -e\n"
               "echo before\n"
               "false\n"
               "echo never\n");
    expect(failing.status == 1 && failing.output == "before\n",
           "set -e ends the script at an untested failure");

    Run tested{"interpret_errexit_tested"};
    tested.go("set -e\n"
              "if false; then echo no; fi\n"
              "false || echo recovered\n"
              "! false\n"
              "echo after\n");
    expect(tested.status == 0 &&
               tested.output == "recovered\nafter\n",
           "a tested context is exempt from set -e");

    Run trapped{"interpret_trap"};
    trapped.go("trap 'echo cleaning' 0\n"
               "echo working\n"
               "exit 4\n");
    expect(trapped.status == 4 &&
               trapped.output == "working\ncleaning\n",
           "the exit trap runs once after the body and keeps the status");

    Run unset{"interpret_nounset"};
    unset.go("set -u\n"
             "echo before\n"
             "echo $missing\n"
             "echo never\n");
    expect(unset.output == "before\n",
           "set -u ends the script on an unset parameter");
}

void builtins_over_real_services() {
    Run run{"interpret_builtins"};
    write_file((run.tree.root() / "captured.in").string(), "a line\n");
    run.go("pwd\n"
           "export SHOWN=yes\n"
           "printf '%s=%s\\n' key value\n"
           "IFS= read -r line < " +
           (run.tree.root() / "captured.in").string() + "\n"
           "echo read [$line]\n"
           "command -v echo\n"
           "shift 1\n"
           "echo left $#\n");
    expect(run.output.find(run.tree.root().string() + "\n") == 0,
           "pwd reports the shell's working directory");
    expect(run.output.find("key=value\n") != std::string::npos,
           "printf formats through the level-3 builtin");
    expect(run.output.find("echo\n") != std::string::npos,
           "command -v names a resolved command");
}

const mm::test::case_ cases[]{
    {"script end to end", &runs_a_script_end_to_end},
    {"redirections and here-documents", &redirections_and_here_documents},
    {"pipelines and externals", &pipelines_and_externals},
    {"substitution and isolation",
     &command_substitution_and_state_isolation},
    {"functions subshells and flow", &functions_subshells_and_flow},
    {"errexit traps and exit", &errexit_traps_and_exit},
    {"builtins over real services", &builtins_over_real_services},
};

const mm::test::registrar reg{"mm.shell.full interpreter", cases};

}  // namespace
