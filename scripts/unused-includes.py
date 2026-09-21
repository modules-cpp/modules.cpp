#!/usr/bin/env python3
"""Report standard headers a translation unit includes but does not use.

    scripts/unused-includes.py [-v] [paths...]

With no paths, every .cpp and .cppm under modules, platforms, tools, tests,
models, and libraries is read. Exit 0 when nothing is reported, 1 when
something is, so it can gate a change.

This is a heuristic, and it is deliberately biased toward keeping a header:
a header is reported only when no symbol it is known to provide appears in
the file. Comments and string literals are removed before matching, so a
header named in prose does not count as use, and both spellings of a name
are searched -- size_t as well as std::size_t -- because either one means
the include is needed.

What it cannot know is whether a header is reachable some other way. A file
using std::string may compile without <string> because <filesystem> brought
it in, and this tool will still report <string> as used, not as removable.
The compiler is the arbiter: remove what this reports, then build.

A header with no rule below is reported separately rather than silently
treated as used. Add a rule when a new header appears in the tree.
"""

import pathlib
import re
import sys

# One pattern per header: what its presence in a file would look like.
RULES = {
    "algorithm": r"std::(sort|stable_sort|find|find_if|find_if_not|any_of|"
        r"all_of|"
                 r"none_of|count|count_if|max|min|max_element|min_element|"
                     r"unique|"
                 r"remove|remove_if|replace|reverse|rotate|copy|copy_if|"
                     r"transform|"
                 r"lower_bound|upper_bound|binary_search|includes|"
                     r"set_difference|"
                 r"set_union|set_intersection|fill|equal|mismatch|clamp|"
                 r"for_each)\b",
    "array": r"std::array\b",
    "cctype": r"std::(isspace|isdigit|isalpha|isalnum|isupper|islower|isblank|"
              r"isprint|ispunct|isxdigit|tolower|toupper)\b",
    "cerrno": r"\berrno\b|\bE[A-Z]{2,}\b",
    "charconv": r"std::(from_chars|to_chars|chars_format)\b",
    "chrono": r"std::chrono\b",
    "cmath": r"std::(abs|fabs|floor|ceil|round|pow|sqrt|sin|cos|tan|log|exp|"
             r"fmod|isnan|isinf|isfinite|isnormal|signbit|trunc|hypot|"
             r"atan2|log2|log10|cbrt|copysign)\b",
    "compare": r"std::(strong_ordering|weak_ordering|partial_ordering)\b",
    "cstddef": r"(std::)?\b(size_t|ptrdiff_t|nullptr_t)\b|std::byte\b",
    "cstdint": r"(std::)?\bu?int(8|16|32|64|max|ptr)_t\b",
    "cstdio": r"(std::)?\bFILE\b|std::(fopen|fclose|fread|fwrite|fprintf|"
        r"printf|"
              r"snprintf|fgets|fputs|feof|ferror|perror|remove|rename)\b|"
              r"\bpopen\s*\(|\bpclose\s*\(",
    "cstdlib": r"(std::)?\b(getenv|system|exit|abort)\s*\(|"
               r"std::(strtol|strtoul|strtod|atoi|atol|malloc|calloc|free|"
                   r"qsort|"
               r"EXIT_SUCCESS|EXIT_FAILURE)\b",
    "cstring": r"std::(strlen|strcmp|strncmp|strcpy|strncpy|memcpy|memmove|"
        r"memset|"
               r"memcmp|strchr|strrchr|strstr|strerror|strcat|strncat)\b",
    "ctime": r"std::(time_t|tm|time|localtime|gmtime|strftime|mktime)\b",
    "exception": r"std::(exception|terminate|current_exception)\b",
    "filesystem": r"std::filesystem\b",
    "fstream": r"std::(ifstream|ofstream|fstream|filebuf)\b",
    "functional": r"std::(function|less|greater|equal_to|hash|ref|cref|invoke|"
                  r"bind|plus|minus)\b",
    "initializer_list": r"std::initializer_list\b",
    "iomanip": r"std::(setw|setfill|setprecision|setbase|quoted)\b",
    "ios": r"std::(ios|streamsize|hex|dec|oct|boolalpha)\b",
    "iostream": r"std::(cout|cerr|cin|clog|endl)\b",
    "istream": r"std::istream\b",
    "iterator": r"std::(begin|end|back_inserter|front_inserter|inserter|"
        r"distance|"
                r"advance|next|prev)\b",
    "limits": r"std::numeric_limits\b",
    "map": r"std::(map|multimap)\b",
    "memory": r"std::(unique_ptr|shared_ptr|weak_ptr|make_unique|make_shared|"
              r"addressof)\b",
    "numeric": r"std::(accumulate|iota|reduce|inner_product|gcd|lcm)\b",
    "optional": r"std::(optional|nullopt)\b",
    "ostream": r"std::ostream\b",
    "set": r"std::(set|multiset)\b",
    "span": r"std::span\b",
    "sstream": r"std::(istringstream|ostringstream|stringstream|stringbuf)\b",
    "stdexcept": r"std::(runtime_error|logic_error|out_of_range|"
                 r"invalid_argument)\b",
    "string": r"std::string\b(?!_view)|std::(to_string|stoi|stol|stoul|stod|"
              r"getline)\b",
    "string_view": r"std::string_view\b",
    "system_error": r"std::(error_code|error_category|errc|system_error)\b",
    "tuple": r"std::(tuple|make_tuple|tie|get)\b",
    "type_traits": r"std::(is_same|enable_if|decay|remove_reference|"
        r"conditional|"
                   r"underlying_type|is_integral)\b",
    "unordered_map": r"std::unordered_map\b",
    "unordered_set": r"std::unordered_set\b",
    "utility": r"std::(move|pair|swap|forward|exchange|make_pair|as_const)\b",
    "variant": r"std::(variant|get_if|holds_alternative|visit)\b",
    "vector": r"std::vector\b",
    # POSIX and system headers used by the host tools.
    "fcntl.h": r"\b(O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|"
        r"O_CLOEXEC|"
               r"O_DIRECTORY|O_NONBLOCK|F_SETFD|F_GETFD|FD_CLOEXEC)\b|"
               r"::(open|openat|fcntl)\s*\(",
    "unistd.h": r"::(read|write|close|unlink|unlinkat|dup2|pipe|fork|execv|"
        r"chdir|"
                r"getcwd|access|isatty|rmdir|symlink|readlink)\s*\(|"
                r"\bSTD(IN|OUT|ERR)_FILENO\b",
    "sys/stat.h": r"\b(S_IS[A-Z]+|S_IR[A-Z]+|S_IW[A-Z]+|mkdir|fstat|stat|lstat|"
                  r"fchmod|umask)\s*\(|\bstruct stat\b",
    "sys/wait.h": r"\b(WEXITSTATUS|WIFEXITED|WIFSIGNALED|WTERMSIG|"
                  r"waitpid)\s*\(",
    "sys/types.h": r"\b(pid_t|mode_t|off_t|ssize_t|uid_t|gid_t)\b",
    "dirent.h": r"\b(opendir|readdir|closedir|DIR)\b",
    "termios.h": r"\b(termios|tcgetattr|tcsetattr|cfmakeraw)\b",
    "poll.h": r"\b(poll|pollfd|POLLIN|POLLOUT)\b",
    "time.h": r"\b(clock_gettime|CLOCK_MONOTONIC|nanosleep|timespec)\b",
}

ROOTS = ("modules", "platforms", "tools", "tests", "models", "libraries",
         "apps")


def strip_noise(text):
    """The file without comments or string literals, so prose does not count."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'R"([^(]*)\(.*?\)\1"', ' "" ', text, flags=re.S)
    # Character literals first: '"' is a quote to C++ and would otherwise open
    # a string literal here, swallowing the code up to the next quote.
    text = re.sub(r"'(\\.|[^'\\])'", " '' ", text)
    text = re.sub(r'"(\\.|[^"\\\n])*"', ' "" ', text)
    return text


def report(path, verbose):
    text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    includes = [(n, h) for n, line in enumerate(text.splitlines(), 1)
                for h in re.findall(r"^#include <([^>]+)>", line)]
    body = strip_noise(re.sub(r"^#include <[^>]+>\n", "", text, flags=re.M))

    unused, unknown = [], []
    for line_number, header in includes:
        pattern = RULES.get(header)
        if pattern is None:
            unknown.append((line_number, header))
        elif not re.search(pattern, body):
            unused.append((line_number, header))

    for line_number, header in unused:
        print(f"{path}:{line_number}: <{header}> included but nothing"
              f" of it is used")
    if verbose:
        for line_number, header in unknown:
            print(f"{path}:{line_number}: <{header}> has no rule; not judged")
    return len(unused)


def main(argv):
    verbose = False
    paths = []
    for arg in argv:
        if arg in ("-v", "--verbose"):
            verbose = True
        elif arg in ("-h", "--help"):
            print(__doc__.strip())
            return 0
        elif arg.startswith("-"):
            print(f"unused-includes: unknown option: {arg}", file=sys.stderr)
            return 64
        else:
            paths.append(arg)

    if not paths:
        for root in ROOTS:
            for pattern in ("*.cpp", "*.cppm"):
                paths.extend(str(p) for p in pathlib.Path(root).rglob(pattern))

    findings = sum(report(path, verbose) for path in sorted(paths))
    print(f"unused-includes: {findings} in {len(paths)} file(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
