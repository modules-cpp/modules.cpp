// modules.cpp build tool, stage 0
//
// Usage: build0            prints its arguments and exits; the smoke test
//                          that proves the host compiler produced a
//                          runnable binary
//        build0 build1     builds out/build1 via the same fixed steps
//                          bootstrap.sh performs by hand
//        build0 -h|--help  prints this interface without starting a build
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
int build_1(const std::string& compiler, const std::string& module_flags)
{
    struct Step {
        std::filesystem::path source;
        std::filesystem::path object;
        std::string module_name;
    };

    const std::vector<Step> steps = {
        {"modules/mm/mdy/mdy.cppm",         "out/modules/mm/mdy/mdy.o", "mm.mdy"},
        {"modules/mm/mdy/src/mdy.cpp",      "out/modules/mm/mdy/src/mdy.o", {}},
        {"modules/mm/build/build.cppm",     "out/modules/mm/build/build.o", "mm.build"},
        {"modules/mm/build/src/build.cpp",  "out/modules/mm/build/src/build.o", {}},
        {"tools/build/build.cpp",           "out/tools/build/build.o", {}},
    };

    for (const auto& step : steps) {
        std::error_code ec;
        std::filesystem::create_directories(step.object.parent_path(), ec);
        if (ec) {
            std::cerr << "build1: cannot create " << step.object.parent_path().string()
                      << ": " << ec.message() << "\n";
            return 5;
        }

        const bool clang = compiler.find("clang") != std::string::npos;
        const bool module_interface = step.source.extension() == ".cppm";
        std::string cmd = shell_quote(compiler) + " " + module_flags;
        if (!clang || !module_interface) cmd += " -x c++";
        if (clang && module_interface) {
            std::string pcm_name = step.module_name;
            for (char& c : pcm_name) if (c == ':') c = '-';
            cmd += " -fmodule-output='out/bootstrap-bmi/" + pcm_name + ".pcm'";
        }
        cmd += " -c " + shell_quote(step.source) +
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

    std::string link_cmd = shell_quote(compiler) + " -std=c++20";
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
    if (argc >= 2 && (std::string_view(argv[1]) == "-h" ||
                      std::string_view(argv[1]) == "--help")) {
        std::cout << "Usage: build0 [-h|--help] [build1]\n";
        return 0;
    }

    std::string compiler = "c++";
    std::string module_flags = "-std=c++20 -fmodules-ts";
    bool build1_requested = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: build0 [build1] [--compiler CXX] [--flags FLAGS]\n";
            return 0;
        }
        if (arg == "build1") {
            if (build1_requested) {
                std::cerr << "build0: build1 may be given only once\n";
                return 2;
            }
            build1_requested = true;
        } else if (arg == "--compiler" || arg == "--cxx") {
            if (++i >= argc || argv[i][0] == '\0') {
                std::cerr << "build0: --compiler requires a value\n";
                return 2;
            }
            compiler = argv[i];
        } else if (arg == "--flags" || arg == "--module-flags") {
            if (++i >= argc) {
                std::cerr << "build0: --flags requires a value\n";
                return 2;
            }
            module_flags = argv[i];
        } else {
            std::cerr << "build0: unknown argument: " << arg << "\n";
            return 2;
        }
    }

    std::cout << "modules.cpp build tool" << "\n";
    for (int i = 0; i < argc; ++i)
        std::cout << argv[i] << "\n";

    if (!build1_requested) {
        if (argc <= 1)
            std::cerr << "no arguments" << "\n";
        return 0;
    }

    std::cout << "build1 compiler " << compiler << "\n";
    std::cout << "build1 flags " << module_flags << "\n";
    return build_1(compiler, module_flags);
}
