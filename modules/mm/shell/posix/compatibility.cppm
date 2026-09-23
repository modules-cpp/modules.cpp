// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string>
#include <vector>

export module mm.shell.posix:compatibility;

export namespace mm::shell {

// One line of a parsed shell script.
enum class LineKind { Empty, Comment, Assignment, Command };

struct ScriptLine {
    LineKind kind = LineKind::Empty;
    std::string text;   // the raw line, unmodified
    std::string name;   // Assignment only: the variable name
    std::string value;  // Assignment only: the right-hand side, quotes stripped
};

// A line is Comment if its first non-blank character is #, Assignment if,
// after leading whitespace, it starts with NAME= for a POSIX-style
// identifier NAME, and Command otherwise. Real shell grammar (quoting
// across lines, command substitution, control flow) is not parsed: a line
// using any of that is still classified, just as a plain Command like any
// other, with none of its internal structure recognized.
[[nodiscard]] std::vector<ScriptLine> parse_script(
    const std::filesystem::path& path);

}  // namespace mm::shell
