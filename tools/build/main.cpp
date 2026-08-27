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

// settigns
std::map<std::string, std::string> settings;
// trim whitespace

std::string trim(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace),s.end());
    return s;
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
    bool bHeaderDone = false;
    while (std::getline(mmfile, line)) {
        std::cout << line << "\n";
        auto pos = line.find(':');
        if (pos != std::string::npos) {
            if (bHeader) {
                auto key = trim(line.substr(0, pos));
                auto val = trim(line.substr(pos+1));
                std::cout << "key " << key << "\n";
                std::cout << "val " << val << "\n";
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
                    bHeaderDone = true;
                }
            }
        }
    }
    std::cout << "compile the file " << "\n";
    std::string mmcpp="c++";
    std::string mmcppflags="-std=c++20";
    std::string sourcefile = settings["file"];
    std::string outfile = settings["name"];
    std::filesystem::path basepath = path.remove_filename();
    std::filesystem::path sourcepath = cwd / basepath / sourcefile;
    std::filesystem::path buildpath = cwd / "out";
    std::filesystem::path outpath = buildpath / outfile;
    std::string cmd = mmcpp + " " + mmcppflags + " " + sourcepath.string() + " -o " + outpath.string() + "\n";
    std::cout << cmd << "\n";
    std::system(cmd.c_str());
}