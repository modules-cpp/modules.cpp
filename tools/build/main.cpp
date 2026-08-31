// modules.cpp build tool, stage 0
//
// Usage: build0 <path to mm.mdy>     compiles the one file: entry it names
//        build0 build1               builds out/build1 via the same fixed
//                                     steps bootstrap.sh performs by hand
//
// build0 exists only to prove the host compiler works before any manifest
// or module exists to build with; see bootstrap.sh and README.md.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <iostream>
#include <vector>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <string>
#include <map>
#include <algorithm>

#include <sys/wait.h>

// settigns
std::map<std::string, std::string> settings;
// trim whitespace

std::string trim(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace),s.end());
    return s;
}

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

// name: is joined to buildpath as a single path segment (never a directory
// component of it), so a "/" in it is as unsafe as ".." or an absolute
// value: any of the three lets an attacker-controlled manifest write
// outside out/.
bool is_safe_name(const std::string& name)
{
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos;
}

// file: is joined to basepath and may legitimately contain directory
// separators (a source file in a subdirectory), so this only rejects what
// name: also rejects for other reasons: absolute values discard basepath
// entirely (std::filesystem::path::operator/ replaces its left operand
// when the right one is absolute), and ".." can climb back out of it.
bool is_safe_relative_path(const std::filesystem::path& raw)
{
    if (raw.empty() || raw.is_absolute()) return false;
    for (const auto& part : raw.lexically_normal())
        if (part == "..") return false;
    return true;
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

    std::string link_cmd = "c++ -std=c++20";
    for (const auto& step : steps) link_cmd += " " + shell_quote(step.object);
    link_cmd += " -o " + shell_quote(std::filesystem::path("out/build1"));

    std::cout << link_cmd << "\n";
    if (run(link_cmd) != 0) {
        std::cerr << "build1: failed to link out/build1\n";
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
    if (args[1] == "build1") return build_1();

    std::cout << "check arguments" << "\n";
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path();

    if (std::filesystem::is_directory(args[1], ec))
        std::cout << "got directory" << "\n";
    else
        std::cout << "not a directory" << "\n";

    std::cout << "check file name" << "\n";
    std::filesystem::path path = args[1];
    std::cout << path.filename() << "\n";
    std::cout << path.stem() << "\n";
    std::cout << path.extension() << "\n";
    std::cout << path.parent_path() << "\n";
    if (path.filename() != "mm.mdy") {
        std::cerr << "file name is not recognized as mm.mdy" << "\n";
        exit(2);
    }
    std::cout << "open file" << "\n";
    std::ifstream mmfile(path);
    if (!mmfile) {
        std::cerr << "failed to open mm.dy file" << "\n";
        exit(3);
    }
    std::cout << "reads file" << "\n";
    std::string line;
    bool bHeader = false;
    while (std::getline(mmfile, line)) {
        std::cout << line << "\n";
        auto pos = line.find(':');
        if (pos != std::string::npos) {
            if (bHeader) {
                auto key = trim(line.substr(0, pos));
                auto val = trim(line.substr(pos+1));
                std::cout << "key " << key << "\n";
                std::cout << "val " << val << "\n";
                // settings is a map: a second "file:" or "name:" entry
                // would silently replace the first rather than being an
                // error, so a manifest declaring either twice is rejected
                // outright instead of picking one arbitrarily.
                if (settings.find(key) != settings.end()) {
                    std::cerr << "duplicate key in front matter: " << key << "\n";
                    exit(6);
                }
                settings[key] = val;
            }
        } else {
            if ( line == "---") {
                if (!bHeader){
                    std::cout << "header starts" << "\n";
                    bHeader = true;
                } else {
                    std::cout << "header ends" << "\n";
                    bHeader = false;
                }
            }
        }
    }
    std::cout << "compile the file " << "\n";
    std::string mmcpp="c++";
    std::string mmcppflags="-std=c++20";
    std::string sourcefile = settings["file"];
    std::string outfile = settings["name"];
    if (!is_safe_relative_path(sourcefile)) {
        std::cerr << "unsafe file: value: " << sourcefile << "\n";
        exit(7);
    }
    if (!is_safe_name(outfile)) {
        std::cerr << "unsafe name: value: " << outfile << "\n";
        exit(8);
    }
    std::filesystem::path basepath = path.remove_filename();
    std::filesystem::path sourcepath = cwd / basepath / sourcefile;
    std::filesystem::path buildpath = cwd / "out";
    std::filesystem::path outpath = buildpath / outfile;
    std::string cmd = mmcpp + " " + mmcppflags + " " + shell_quote(sourcepath) + " -o " + shell_quote(outpath);
    std::cout << cmd << "\n";
    const int status = run(cmd);
    if (status != 0) {
        std::cerr << "failed to compile " << sourcepath.string() << "\n";
        return 4;
    }
}