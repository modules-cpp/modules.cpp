// modules.cpp build tool, stage 0
//
// Usage: build0            prints its arguments and exits; the smoke test
//                          that proves the host compiler produced a
//                          runnable binary
//        build0 build1     builds out/build1 via the same fixed steps
//                          bootstrap.sh performs by hand
//
// build0 exists only to prove the host compiler works and to reach build1
// before any manifest or module exists to build with; see bootstrap.sh and
// README.md. It deliberately supports nothing else: a general
// manifest-driven mode here would duplicate mm.build's manifest handling,
// including every path rule mm.build enforces, in a stage that cannot
// import it.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <iostream>
#include <vector>
#include <string_view>
#include <filesystem>
#include <string>

#include <sys/wait.h>

// Single quotes disable every form of shell expansion; a single quote in the
// text is closed, escaped, and reopened. Matches mm::build::shell_quote,
// which this stage 0 tool cannot import: it is compiled standalone, before
// any module exists to import.
std::string shell_quote(const std::filesystem::path& path)
{
    const std::string& text = path.native();

    std::string quoted;
    quoted.reserve(text.size() + 2);

    quoted += '\'';
    for (const char c : text) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';

    return quoted;
}

// Runs command through /bin/sh, returning its exit code rather than the raw
// wait status std::system() hands back.
int run(const std::string& command)
{
    const int status = std::system(command.c_str());
    if (status == -1) return -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

// Compiles and links build1 through the exact fixed steps as bootstrap.sh.
// The mm.mdy and mm.build module interfaces and their
// implementation units, then tools/build/build.cpp (the same source
// tools/build/mm.mdy declares as the "build" app target), in the order
// -fmodules-ts needs an interface compiled before whatever imports it.
//
int build_1()
{
    struct Step {
        std::filesystem::path source;
        std::filesystem::path object;
    };

    const std::vector<Step> steps = {
        {"modules/mm/mdy/mdy.cppm",         "out/modules/mm/mdy/mdy.o"},
        {"modules/mm/mdy/src/mdy.cpp",      "out/modules/mm/mdy/src/mdy.o"},
        {"modules/mm/build/build.cppm",     "out/modules/mm/build/build.o"},
        {"modules/mm/build/src/build.cpp",  "out/modules/mm/build/src/build.o"},
        {"tools/build/build.cpp",           "out/tools/build/build.o"},
    };

    const std::string module_compiler = "c++ -fmodules-ts";
    const std::string module_flags = "-std=c++20 -x c++";

    for (const auto& step : steps) {
        std::error_code ec;
        std::filesystem::create_directories(step.object.parent_path(), ec);
        if (ec) {
            std::cerr << "build1: cannot create " << step.object.parent_path().string()
                      << ": " << ec.message() << "\n";
            return 5;
        }

        const std::string cmd = module_compiler + " " + module_flags +
                                 " -c " + shell_quote(step.source) +
                                 " -o " + shell_quote(step.object);
        std::cout << cmd << "\n";
        if (run(cmd) != 0) {
            std::cerr << "build1: failed to compile " << step.source.string() << "\n";
            return 5;
        }
    }

    // Linked to a temporary and renamed only on success, matching
    // mm::build::link: a failed link must not leave a partial out/build1
    // that bootstrap.sh's existence check, or a later run, would accept as
    // a working one.
    const std::filesystem::path output = "out/build1";
    const std::filesystem::path temp = "out/build1.tmp";

    std::error_code ec;
    std::filesystem::remove(temp, ec);

    std::string link_cmd = "c++ -std=c++20";
    for (const auto& step : steps) link_cmd += " " + shell_quote(step.object);
    link_cmd += " -o " + shell_quote(temp);

    std::cout << link_cmd << "\n";
    if (run(link_cmd) != 0) {
        std::cerr << "build1: failed to link " << output.string() << "\n";
        std::filesystem::remove(temp, ec);
        return 5;
    }

    std::filesystem::rename(temp, output, ec);
    if (ec) {
        std::cerr << "build1: cannot move " << temp.string() << " into place: "
                  << ec.message() << "\n";
        std::filesystem::remove(temp, ec);
        return 5;
    }

    return 0;
}

// main
int main(int argc, char** argv)
{
    std::cout << "modules.cpp build tool" << "\n";
    // arguments
    std::vector<std::string_view> args(argv, argv+argc);
    // print arguments
    for (std::string_view arg : args)
        std::cout << arg << "\n";
    // if no arguments quit
    if (args.size() <= 1) {
        std::cerr << "no arguments" << "\n";
        exit(0);
    }
    if (args.size() > 2) {
        std::cerr << "too many arguments" << "\n";
        exit(1);
    }
    if (args[1] != "build1") {
        std::cerr << "unknown argument: " << args[1] << "\n";
        std::cerr << "usage: build0 [build1]\n";
        return 2;
    }

    return build_1();
}
