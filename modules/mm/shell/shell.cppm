// Reusable shell integration: locating the user's interactive shell,
// reading and writing this process's environment variables, and a small
// parser for this project's own *.sh scripts. mm::build::run already wraps
// /bin/sh for command execution; this module is for callers that need to
// inspect or influence the environment those commands run in, or need to
// read a script's own content rather than merely execute it.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell;

export namespace mm::shell {

// The shell named by $SHELL: this process's interactive shell, not the one
// mm::build::run invokes. POSIX system(), which run wraps, always uses
// /bin/sh regardless of $SHELL. Empty if $SHELL is unset.
[[nodiscard]] std::filesystem::path current_shell();

// Wraps the POSIX getenv/setenv/unsetenv functions <cstdlib> exposes on this
// platform, so callers reach the process environment through one interface
// rather than each holding its own #include <cstdlib>. set and unset
// mutate this process's real environment, so a command mm::build::run
// launches afterward inherits the change.
[[nodiscard]] std::optional<std::string> get(std::string_view name);
[[nodiscard]] bool set(std::string_view name, std::string_view value, bool overwrite = true);
[[nodiscard]] bool unset(std::string_view name);

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
// other, with none of its internal structure recognized. This function has
// no caller among the project's own *.sh scripts today (only its own test
// suite exercises it): several of them use command substitution and
// control flow (if, for, case), which this classifier does not need to
// understand for its own purpose, but would not parse in any deeper sense
// if it were ever pointed at them.
[[nodiscard]] std::vector<ScriptLine> parse_script(const std::filesystem::path& path);

}  // namespace mm::shell
