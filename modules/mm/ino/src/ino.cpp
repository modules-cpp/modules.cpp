// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

module mm.ino;

import mm.mdy;

namespace mm::ino {

namespace {

std::string_view trim_leading(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    return sv;
}

std::string_view trim_trailing(std::string_view sv) {
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_part(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

bool is_keyword(std::string_view word) {
    static const std::string_view keywords[] = {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "return", "break", "continue", "goto", "class", "struct", "enum",
        "union", "template", "using", "typedef", "namespace", "public",
        "private", "protected", "operator", "sizeof"
    };
    for (const auto& kw : keywords) {
        if (word == kw) return true;
    }
    return false;
}

std::vector<std::string> split_lines(std::string_view content) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < content.size()) {
        std::size_t end = content.find('\n', start);
        if (end == std::string_view::npos) {
            std::string_view line = content.substr(start);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            lines.emplace_back(line);
            break;
        }
        std::string_view line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.emplace_back(line);
        start = end + 1;
    }
    return lines;
}

// Parses a line at brace depth 0:
// Returns true if recognized as a prototype or definition.
// sets is_definition, is_column_zero, fn_name, prototype_str
bool parse_fn_decl(std::string_view line,
                   std::string_view next_line,
                   bool& is_definition,
                   bool& is_column_zero,
                   std::string& fn_name,
                   std::string& prototype_str) {
    if (line.empty()) return false;
    is_column_zero = (line.front() != ' ' && line.front() != '\t');

    std::size_t start_idx = 0;
    while (start_idx < line.size() && (line[start_idx] == ' ' || line[start_idx] == '\t')) {
        ++start_idx;
    }
    if (start_idx >= line.size()) return false;

    // Find the first open parenthesis '('
    std::size_t open_paren = line.find('(', start_idx);
    if (open_paren == std::string_view::npos || open_paren == start_idx) return false;

    // Parse identifier immediately before '('
    std::size_t name_end = open_paren;
    while (name_end > start_idx && (line[name_end - 1] == ' ' || line[name_end - 1] == '\t')) {
        --name_end;
    }
    if (name_end == start_idx) return false;

    std::size_t name_start = name_end;
    while (name_start > start_idx && is_identifier_part(line[name_start - 1])) {
        --name_start;
    }
    if (name_start == name_end || !is_identifier_start(line[name_start])) return false;

    std::string_view name = line.substr(name_start, name_end - name_start);
    if (is_keyword(name) || name == "operator") return false;

    // Everything before name_start is the return type
    std::string_view return_type_part = line.substr(start_idx, name_start - start_idx);
    std::string_view trimmed_ret = trim_trailing(trim_leading(return_type_part));
    if (trimmed_ret.empty()) return false;

    // Return type cannot contain template brackets, control keywords, or equals
    if (trimmed_ret.find('<') != std::string_view::npos ||
        trimmed_ret.find('>') != std::string_view::npos ||
        trimmed_ret.find('=') != std::string_view::npos) {
        return false;
    }

    // Check return type tokens: only identifiers, *, &, const, ::
    for (std::size_t i = 0; i < trimmed_ret.size();) {
        char c = trimmed_ret[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == '*' || c == '&') {
            ++i;
            continue;
        }
        if (c == ':' && i + 1 < trimmed_ret.size() && trimmed_ret[i + 1] == ':') {
            i += 2;
            continue;
        }
        if (is_identifier_start(c)) {
            std::size_t token_start = i;
            while (i < trimmed_ret.size() && is_identifier_part(trimmed_ret[i])) ++i;
            std::string_view token = std::string_view(trimmed_ret).substr(token_start, i - token_start);
            if (is_keyword(token)) return false;
            continue;
        }
        return false; // Unknown character in return type
    }

    // Match closing parenthesis ')'
    int paren_depth = 0;
    std::size_t close_paren = std::string_view::npos;
    for (std::size_t i = open_paren; i < line.size(); ++i) {
        if (line[i] == '(') {
            ++paren_depth;
        } else if (line[i] == ')') {
            --paren_depth;
            if (paren_depth == 0) {
                close_paren = i;
                break;
            }
        }
    }
    if (close_paren == std::string_view::npos) return false;

    // After close_paren: check what follows
    std::string_view after_paren = line.substr(close_paren + 1);
    std::string_view trimmed_after = trim_trailing(trim_leading(after_paren));

    // Could have const or noexcept or trailing attributes before { or ;
    bool has_brace = false;
    bool has_semicolon = false;

    if (!trimmed_after.empty()) {
        if (trimmed_after.back() == '{') {
            has_brace = true;
        } else if (trimmed_after.back() == ';') {
            has_semicolon = true;
        } else if (trimmed_after.find('{') != std::string_view::npos) {
            has_brace = true;
        } else if (trimmed_after.find(';') != std::string_view::npos) {
            has_semicolon = true;
        }
    } else {
        // Next line might start with {
        std::string_view trimmed_next = trim_leading(next_line);
        if (!trimmed_next.empty() && trimmed_next.front() == '{') {
            has_brace = true;
        }
    }

    if (!has_brace && !has_semicolon) return false;

    fn_name = std::string(name);
    is_definition = has_brace;

    // Build prototype string: trimmed_ret + " " + fn_name + "(" + params + ");"
    std::string_view params = line.substr(open_paren, close_paren - open_paren + 1);
    prototype_str = std::string(trimmed_ret) + " " + fn_name + std::string(params) + ";";
    return true;
}

} // namespace

TransformResult transform(std::span<const SourceFile> sources) {
    TransformResult result;
    result.ok = true;

    // Each entry is a complete include spec with its delimiters, either
    // <header> or "header", kept in the order the sketches introduce them.
    std::vector<std::string> hoisted_includes;
    std::vector<std::string> existing_prototypes;
    std::vector<std::string> generated_prototypes;

    struct ProcessedFile {
        std::string path;
        std::vector<std::string> lines;
        std::vector<bool> skip_line;
    };
    std::vector<ProcessedFile> processed_files;
    processed_files.reserve(sources.size());

    // First pass: parse comments, includes, prototypes, and definitions
    for (const auto& src : sources) {
        ProcessedFile pf;
        pf.path = src.path;
        pf.lines = split_lines(src.content);
        pf.skip_line.resize(pf.lines.size(), false);

        bool in_block_comment = false;
        int brace_depth = 0;
        bool in_template = false;

        for (std::size_t line_idx = 0; line_idx < pf.lines.size(); ++line_idx) {
            const std::string& line = pf.lines[line_idx];
            const std::size_t line_num = line_idx + 1;

            std::string_view trimmed = trim_leading(line);

            if (!in_block_comment && brace_depth == 0
                && trimmed.starts_with("template")) {
                in_template = true;
            }

            // Check if this line is an include directive (must be at top level outside block comment)
            if (!in_block_comment && trimmed.starts_with("#include")) {
                std::string_view inc_rest = trim_leading(trimmed.substr(8));
                const char closing = inc_rest.starts_with("<")    ? '>'
                                     : inc_rest.starts_with("\"") ? '"'
                                                                  : '\0';
                if (closing != '\0') {
                    std::size_t close = inc_rest.find(closing, 1);
                    if (close != std::string_view::npos) {
                        std::string spec =
                            std::string(inc_rest.substr(0, close + 1));
                        if (std::find(hoisted_includes.begin(),
                                      hoisted_includes.end(),
                                      spec) == hoisted_includes.end()) {
                            hoisted_includes.push_back(spec);
                        }
                        pf.skip_line[line_idx] = true;
                    }
                }
            }

            // If we are at brace depth 0 and not in block comment or template,
            // check for definitions / prototypes
            if (brace_depth == 0 && !in_block_comment && !in_template) {
                std::string_view next_line = (line_idx + 1 < pf.lines.size()) ? std::string_view(pf.lines[line_idx + 1]) : std::string_view{};
                bool is_definition = false;
                bool is_column_zero = false;
                std::string fn_name;
                std::string proto_str;

                if (parse_fn_decl(line, next_line, is_definition, is_column_zero, fn_name, proto_str)) {
                    if (is_definition) {
                        if (fn_name == "main") {
                            result.ok = false;
                            result.diagnostics.push_back({
                                src.path,
                                line_num,
                                "sketch cannot define main",
                                false
                            });
                        } else if (!is_column_zero) {
                            result.diagnostics.push_back({
                                src.path,
                                line_num,
                                "definition not at column zero; no prototype generated; indent it to column zero or declare it",
                                true
                            });
                        } else {
                            if (std::find(existing_prototypes.begin(), existing_prototypes.end(), fn_name) == existing_prototypes.end() &&
                                std::find(generated_prototypes.begin(), generated_prototypes.end(), proto_str) == generated_prototypes.end()) {
                                generated_prototypes.push_back(proto_str);
                            }
                        }
                    } else {
                        // It's a prototype
                        if (std::find(existing_prototypes.begin(), existing_prototypes.end(), fn_name) == existing_prototypes.end()) {
                            existing_prototypes.push_back(fn_name);
                        }
                    }
                }
            }

            // Update brace depth and comment state across line
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (in_block_comment) {
                    if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                        in_block_comment = false;
                        ++i;
                    }
                    continue;
                }
                if (line[i] == '/' && i + 1 < line.size()) {
                    if (line[i + 1] == '/') {
                        break; // Line comment ends processing for this line
                    }
                    if (line[i + 1] == '*') {
                        in_block_comment = true;
                        ++i;
                        continue;
                    }
                }
                if (line[i] == '"') {
                    ++i;
                    while (i < line.size() && line[i] != '"') {
                        if (line[i] == '\\' && i + 1 < line.size()) ++i;
                        ++i;
                    }
                    continue;
                }
                if (line[i] == '\'') {
                    ++i;
                    while (i < line.size() && line[i] != '\'') {
                        if (line[i] == '\\' && i + 1 < line.size()) ++i;
                        ++i;
                    }
                    continue;
                }
                if (line[i] == '{') {
                    ++brace_depth;
                } else if (line[i] == '}') {
                    if (brace_depth > 0) --brace_depth;
                }
            }
            if (in_template && (line.find('{') != std::string::npos
                                || line.find(';') != std::string::npos)) {
                in_template = false;
            }
        }
        processed_files.push_back(std::move(pf));
    }

    if (!result.ok) {
        return result;
    }

    // Assemble output
    std::string out;

    // 1. Header
    out += "// Generated by sketch (mm: 1.3) from ";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::filesystem::path(sources[i].path).filename().string();
    }
    out += " -- do not edit by hand.\n";

    // 2. Hoisted includes
    for (const auto& inc : hoisted_includes) {
        out += "#include " + inc + "\n";
    }

    // 3. The compatibility header, which the Arduino toolchain supplies to a
    // sketch without being asked and which gives the sketch text below the
    // spellings mm.sketch does not export by itself. It is the last include,
    // because it imports mm.sketch and a standard header included after an
    // import is what the hoist above exists to avoid.
    out += "#include \"Arduino.h\"\n";

    // 4. Prelude
    out += "import mm.sketch;\n";
    out += "using namespace mm::sketch;\n";

    // 5. Prototypes
    for (const auto& proto : generated_prototypes) {
        out += proto + "\n";
    }

    // Helper to count newlines in out
    const auto count_newlines = [](std::string_view sv) {
        std::size_t count = 0;
        for (char c : sv) {
            if (c == '\n') ++count;
        }
        return count;
    };

    // 6. Sketch text file by file
    for (const auto& pf : processed_files) {
        std::size_t current_lines = count_newlines(out);
        std::size_t displacement = current_lines + 1;
        out += "// " + std::filesystem::path(pf.path).filename().string() +
               " (displaced by " + std::to_string(displacement) + " lines)\n";
        for (std::size_t i = 0; i < pf.lines.size(); ++i) {
            if (pf.skip_line[i]) {
                out += "\n";
                continue;
            }
            out += pf.lines[i] + "\n";
        }
    }

    // 7. Main
    out += "int main() { return mm::sketch::run(&setup, &loop); }\n";

    result.output = std::move(out);
    return result;
}

namespace {
const std::vector<std::string>* lookup(const mm::mdy::MDYDocument& doc, std::string_view key) {
    auto it = doc.metadata.find(key);
    if (it != doc.metadata.end()) return &it->second;
    return nullptr;
}
}

std::string sketch_header() {
    std::string out;
    out += "// Generated by sketch (mm: 1.3) -- do not edit by hand.\n";
    out += "//\n";
    out += "// The header the Arduino toolchain supplies to every sketch and\n";
    out += "// that a vendored library includes by name, unconditionally,\n";
    out += "// because its own toolchain provides one. modules.cpp does not,\n";
    out += "// so sketch writes this one beside main.cpp, which includes it,\n";
    out += "// and the build puts this directory on the include path. Names\n";
    out += "// below come from mm.sketch, <cstdint>, and the compatibility\n";
    out += "// environment.\n";
    out += "#pragma once\n";
    out += "\n";
    out += "#include <cstddef>\n";
    out += "#include <cstdint>\n";
    out += "\n";
    out += "import mm.sketch;\n";
    out += "\n";
    out += "using namespace mm::sketch;\n";
    out += "\n";
    out += "// <cstdint> guarantees these names in namespace std and\n";
    out += "// leaves the global ones unspecified. A vendored library\n";
    out += "// writing unqualified uint8_t relies on a compiler rather\n";
    out += "// than on the language without these.\n";
    out += "using std::int8_t;    using std::uint8_t;\n";
    out += "using std::int16_t;   using std::uint16_t;\n";
    out += "using std::int32_t;   using std::uint32_t;\n";
    out += "using std::int64_t;   using std::uint64_t;\n";
    out += "using std::size_t;    using std::ptrdiff_t;\n";
    out += "\n";
    out += "// Flash-string spellings. docs/modules-sketch.mdy declines the\n";
    out += "// behaviour, not the spelling: placement is the linker's\n";
    out += "// business here, so each of these is inert.\n";
    out += "#define F(string_literal) (string_literal)\n";
    out += "#define PSTR(string_literal) (string_literal)\n";
    out += "#define PROGMEM\n";
    out += "\n";
    out += "// Analog channel names, numbered in declaration order. A board's\n";
    out += "// real mapping belongs to mm.mcu; until it is asked, a sketch\n";
    out += "// naming a channel the board lacks fails when it reads it\n";
    out += "// rather than when it names it.\n";
    for (int channel = 0; channel < 8; ++channel) {
        out += "inline constexpr unsigned int A" + std::to_string(channel) +
               " = " + std::to_string(channel) + ";\n";
    }
    out += "\n";

    // The mathematical constants the Arduino toolchain defines as macros. A
    // sketch writes PI and expects it to be there; mm.sketch exports the
    // functions of docs/modules-sketch.mdy's Math and trigonometry section
    // but names none of these values.
    out += "// Mathematical constants, spelled as a sketch spells them.\n";
    out += "// docs/modules-sketch.mdy exports the trigonometry; these are\n";
    out += "// the values it takes, at the precision a double can hold.\n";
    out += "inline constexpr double PI = 3.1415926535897932384626433832795;\n";
    out += "inline constexpr double HALF_PI = "
           "1.5707963267948966192313216916398;\n";
    out += "inline constexpr double TWO_PI = "
           "6.283185307179586476925286766559;\n";
    out += "inline constexpr double EULER = "
           "2.718281828459045235360287471352;\n";
    out += "inline constexpr double DEG_TO_RAD = "
           "0.017453292519943295769236907684886;\n";
    out += "inline constexpr double RAD_TO_DEG = "
           "57.295779513082320876798154814105;\n";
    out += "\n";

    // The cooperative hook a sketch calls inside its own waiting loop. The
    // Arduino toolchain spells it yield; this project spells the same
    // behaviour dispatch, and docs/modules-sketch.mdy describes it there.
    out += "// The foreign spelling of dispatch, which is what a sketch\n";
    out += "// calls to let handlers run inside a wait of its own.\n";
    out += "inline void yield() { dispatch(); }\n";
    out += "\n";

    // AVR's binary.h, which a sketch reaches through Arduino.h. Every bit
    // pattern from one to eight digits is a separate name there, so every
    // one of them is a separate name here.
    out += "// Bit patterns one to eight digits wide, as AVR's binary.h\n";
    out += "// spells them. Constants rather than macros: each has a type,\n";
    out += "// a scope, and a value the compiler can see.\n";
    for (int width = 1; width <= 8; ++width) {
        const unsigned int count = 1u << width;
        for (unsigned int value = 0; value < count; ++value) {
            std::string digits;
            for (int bit = width - 1; bit >= 0; --bit)
                digits += ((value >> bit) & 1u) ? '1' : '0';
            const std::string declarator =
                "B" + digits + " = " + std::to_string(value);
            // Four to a line: one name per line reads no better and runs to
            // five hundred of them. Only the first line of a width opens the
            // declaration; the rest are further declarators in it.
            if (value == 0) {
                out += "inline constexpr unsigned int ";
            } else if (value % 4 == 0) {
                out += ",\n    ";
            } else {
                out += ", ";
            }
            out += declarator;
        }
        out += ";\n";
    }
    return out;
}

bool check_application(const std::filesystem::path& app_dir,
                       const mm::mdy::MDYDocument& doc,
                       std::string& error) {
    const auto* sketches = lookup(doc, "sketch");
    if (sketches == nullptr || sketches->empty()) {
        error = "no sketch: entries in manifest";
        return false;
    }

    std::vector<std::string> sketch_files;
    for (const auto& s : *sketches) {
        sketch_files.push_back(s);
    }

    // Check directory for unmanifested .ino files
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(app_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ino") {
            const std::string name = entry.path().filename().string();
            if (std::find(sketch_files.begin(), sketch_files.end(), name) == sketch_files.end()) {
                error = "unmanifested .ino file: " + name;
                return false;
            }
        }
    }

    // Read sketch sources
    std::vector<SourceFile> sources;
    sources.reserve(sketch_files.size());
    for (const auto& file : sketch_files) {
        std::filesystem::path p = app_dir / file;
        std::ifstream in(p);
        if (!in) {
            error = "cannot open sketch file: " + file;
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        sources.push_back({file, ss.str()});
    }

    const auto result = transform(sources);
    if (!result.ok) {
        error = "transformation failed";
        if (!result.diagnostics.empty()) {
            error += ": " + result.diagnostics.front().message;
        }
        return false;
    }

    std::filesystem::path main_cpp = app_dir / "main.cpp";
    std::ifstream in_main(main_cpp);
    if (!in_main) {
        error = "missing generated main.cpp in " + app_dir.string();
        return false;
    }
    std::ostringstream ss_main;
    ss_main << in_main.rdbuf();
    if (ss_main.str() != result.output) {
        error = "committed main.cpp does not match sketch in " + app_dir.string();
        return false;
    }

    // The compatibility header is generated for the same reason and on the
    // same terms as main.cpp, so it is verified on the same terms too. Every
    // sketch application has one, because main.cpp includes it whether or not
    // a library is reached.
    const std::filesystem::path header = app_dir / "Arduino.h";
    std::ifstream in_header(header);
    if (!in_header) {
        error = "missing generated Arduino.h in " + app_dir.string();
        return false;
    }
    std::ostringstream ss_header;
    ss_header << in_header.rdbuf();
    if (ss_header.str() != sketch_header()) {
        error = "committed Arduino.h does not match this release in " +
                app_dir.string();
        return false;
    }

    return true;
}

bool is_library_root(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto props = dir / "library.properties";
    const auto json = dir / "library.json";
    const auto examples = dir / "examples";

    const bool has_props = std::filesystem::is_regular_file(props, ec)
                           && !std::filesystem::is_symlink(props, ec)
                           && !ec;
    ec.clear();
    const bool has_json = std::filesystem::is_regular_file(json, ec)
                          && !std::filesystem::is_symlink(json, ec)
                          && !ec;
    ec.clear();
    const bool has_examples = std::filesystem::is_directory(examples, ec)
                              && !std::filesystem::is_symlink(examples, ec)
                              && !ec;
    return (has_props || has_json) && has_examples;
}

namespace {

bool is_inside(const std::filesystem::path& base,
               const std::filesystem::path& p) {
    std::error_code ec;
    auto can_base = std::filesystem::weakly_canonical(base, ec);
    if (ec) return false;
    auto can_p = std::filesystem::weakly_canonical(p, ec);
    if (ec) return false;
    auto rel = can_p.lexically_relative(can_base);
    return !rel.empty() && !rel.is_absolute()
           && *rel.begin() != "..";
}

bool is_valid_manifest_name(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const unsigned char c : name) {
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return true;
}

bool is_valid_sketch_filename(std::string_view filename) {
    if (filename.empty() || !filename.ends_with(".ino")) return false;
    for (const unsigned char c : filename) {
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return true;
}

bool walk_examples_dir(const std::filesystem::path& current_dir,
                       const std::filesystem::path& rel_from_examples,
                       const std::filesystem::path& library_root,
                       LibraryPlan& plan,
                       std::vector<std::string>& yielded_names) {
    std::error_code ec;
    std::vector<std::string> local_sketches;
    std::vector<std::filesystem::path> subdirs;

    for (const auto& entry :
         std::filesystem::directory_iterator(current_dir, ec)) {
        if (ec) {
            plan.ok = false;
            plan.error = "cannot read directory: " + current_dir.string();
            return false;
        }
        std::error_code sec;
        const auto status = entry.symlink_status(sec);
        if (sec || std::filesystem::is_symlink(status)) {
            plan.skipped.push_back("symlink skipped: "
                                  + entry.path().string());
            continue;
        }

        const auto filename = entry.path().filename().string();
        if (filename.empty() || filename.front() == '.') {
            continue;
        }

        if (!is_inside(library_root, entry.path())) {
            plan.skipped.push_back("outside library root: "
                                  + entry.path().string());
            continue;
        }

        if (std::filesystem::is_regular_file(status)) {
            if (entry.path().extension() == ".ino") {
                if (!is_valid_sketch_filename(filename)) {
                    plan.skipped.push_back("invalid sketch filename skipped: "
                                          + entry.path().string());
                    continue;
                }
                local_sketches.push_back(filename);
            }
        } else if (std::filesystem::is_directory(status)) {
            if (!is_valid_manifest_name(filename)) {
                plan.skipped.push_back("invalid directory name skipped: "
                                      + entry.path().string());
                continue;
            }
            subdirs.push_back(entry.path());
        }
    }

    if (rel_from_examples.empty() && !local_sketches.empty()) {
        plan.ok = false;
        plan.error = "sketch directly under examples/ is not permitted: "
                     + (current_dir / local_sketches.front()).string()
                     + "; each sketch must reside in its own subdirectory";
        return false;
    }

    if (!local_sketches.empty()) {
        const std::string dir_base = current_dir.filename().string();
        const std::string cand_ino = dir_base + ".ino";

        std::string primary;
        auto cand_it = std::find(local_sketches.begin(),
                                 local_sketches.end(), cand_ino);
        if (cand_it != local_sketches.end()) {
            primary = *cand_it;
        } else if (local_sketches.size() == 1) {
            primary = local_sketches.front();
        } else {
            std::string cands;
            for (const auto& s : local_sketches) {
                if (!cands.empty()) cands += ", ";
                cands += s;
            }
            plan.skipped.push_back("ambiguous primary sketch in "
                                  + current_dir.string()
                                  + "; candidates: " + cands);
            return false;
        }

        std::vector<std::string> remaining;
        for (const auto& s : local_sketches) {
            if (s != primary) remaining.push_back(s);
        }
        std::sort(remaining.begin(), remaining.end());

        LibraryAppNode app;
        app.dir = current_dir;
        app.rel_path = rel_from_examples.generic_string();
        std::string hyp_name = app.rel_path;
        for (char& c : hyp_name) {
            if (c == '/') c = '-';
        }
        app.name = hyp_name;
        if (!is_valid_manifest_name(app.name)) {
            plan.skipped.push_back("invalid derived application name: "
                                  + app.name);
            return false;
        }
        app.sketches.push_back(primary);
        for (const auto& rem : remaining) {
            app.sketches.push_back(rem);
        }

        app.sketch_library_rel =
            library_root.lexically_relative(current_dir).generic_string();

        plan.app_nodes.push_back(std::move(app));
        yielded_names.push_back(dir_base);
        return true;
    }

    std::sort(subdirs.begin(), subdirs.end());
    std::vector<std::string> child_yielded;
    for (const auto& sub : subdirs) {
        const std::string sub_name = sub.filename().string();
        const auto sub_rel = rel_from_examples.empty()
                                 ? std::filesystem::path(sub_name)
                                 : (rel_from_examples / sub_name);
        walk_examples_dir(sub, sub_rel, library_root, plan, child_yielded);
    }

    if (!child_yielded.empty()) {
        std::sort(child_yielded.begin(), child_yielded.end());
        LibraryDirNode node;
        node.dir = current_dir;
        node.name = current_dir.filename().string();
        node.folders = std::move(child_yielded);
        node.is_root = false;
        plan.dir_nodes.push_back(std::move(node));
        yielded_names.push_back(current_dir.filename().string());
        return true;
    }

    plan.skipped.push_back("directory holding no sketch skipped: "
                          + current_dir.string());
    return false;
}

} // namespace

LibraryPlan discover_library(const std::filesystem::path& library_root) {
    LibraryPlan plan;
    std::error_code ec;
    auto abs_root = std::filesystem::weakly_canonical(library_root, ec);
    if (ec) {
        plan.ok = false;
        plan.error = "cannot canonicalize: " + library_root.string();
        return plan;
    }

    if (!is_valid_manifest_name(abs_root.filename().string())) {
        plan.ok = false;
        plan.error = "invalid library name: " + abs_root.filename().string();
        return plan;
    }

    const auto examples_dir = abs_root / "examples";
    if (!std::filesystem::is_directory(examples_dir, ec) || ec) {
        plan.ok = false;
        plan.error = "examples directory not found: "
                     + examples_dir.string();
        return plan;
    }

    plan.root_node.dir = abs_root;
    plan.root_node.name = abs_root.filename().string();
    plan.root_node.folders = {"examples"};
    plan.root_node.is_root = true;

    std::vector<std::string> examples_yielded;
    walk_examples_dir(examples_dir, "", abs_root, plan, examples_yielded);

    std::set<std::string> application_names;
    for (const auto& app : plan.app_nodes) {
        if (!application_names.insert(app.name).second) {
            plan.ok = false;
            plan.error = "derived application name collision: " + app.name;
            plan.dir_nodes.clear();
            return plan;
        }
    }

    if (plan.app_nodes.empty()) {
        plan.ok = false;
        if (plan.error.empty()) {
            plan.error = "no valid sketch applications found under "
                         + examples_dir.string();
        }
        plan.root_node.folders.clear();
        plan.dir_nodes.clear();
        return plan;
    }

    return plan;
}

std::string render_root_manifest(const LibraryDirNode& root,
                                 std::string_view project_rel) {
    if (!is_valid_manifest_name(root.name)) return "";
    for (const unsigned char c : project_rel) {
        if (c < 0x20 || c == 0x7f) return "";
    }
    std::string out = "---\nmm: 1.3\nkind: dir\nname: " + root.name + "\n";
    if (!project_rel.empty()) {
        out += "project: " + std::string(project_rel) + "\n";
    }
    for (const auto& f : root.folders) {
        if (!is_valid_manifest_name(f)) return "";
        out += "folder: " + f + "\n";
    }
    out += "---\n";
    return out;
}

std::string render_dir_manifest(const LibraryDirNode& node) {
    if (!is_valid_manifest_name(node.name)) return "";
    std::string out = "---\nmm: 1.3\nkind: dir\nname: " + node.name + "\n";
    for (const auto& f : node.folders) {
        if (!is_valid_manifest_name(f)) return "";
        out += "folder: " + f + "\n";
    }
    out += "---\n";
    return out;
}

std::string render_app_manifest(const LibraryAppNode& node) {
    if (!is_valid_manifest_name(node.name)) return "";
    for (const unsigned char c : node.sketch_library_rel) {
        if (c < 0x20 || c == 0x7f) return "";
    }
    std::string out = "---\nmm: 1.3\nkind: app\nname: " + node.name + "\n";
    out += "use: mm.sketch\n";
    out += "file: main.cpp\n";
    for (const auto& s : node.sketches) {
        if (!is_valid_sketch_filename(s)) return "";
        out += "sketch: " + s + "\n";
    }
    out += "sketch-library: " + node.sketch_library_rel + "\n";
    out += "---\n";
    return out;
}

bool validate_manifest_compatibility(
    const mm::mdy::MDYDocument& doc,
    const LibraryDirNode* dir_node,
    const LibraryAppNode* app_node,
    bool expect_project,
    const std::filesystem::path& library_root,
    const std::filesystem::path& manifest_path,
    std::string& error,
    const std::filesystem::path& expected_project_root) {

    error.clear();
    if (dir_node != nullptr) {
        const auto* kind = lookup(doc, "kind");
        if (kind == nullptr || kind->size() != 1 ||
            kind->front() != "dir") {
            error = manifest_path.string() + ": expected kind: dir";
            return false;
        }
        const auto* name = lookup(doc, "name");
        if (name == nullptr || name->size() != 1 ||
            name->front() != dir_node->name) {
            error = manifest_path.string() + ": expected name: "
                    + dir_node->name;
            return false;
        }
        if (dir_node->is_root) {
            const auto* proj = lookup(doc, "project");
            if (expect_project) {
                if (proj == nullptr || proj->size() != 1 ||
                    proj->front().empty()) {
                    error = manifest_path.string()
                            + ": expected one project: declaration";
                    return false;
                }
                const std::filesystem::path raw_project(proj->front());
                if (raw_project.is_absolute()) {
                    error = manifest_path.string()
                            + ": project: value must be relative";
                    return false;
                }
                if (!expected_project_root.empty()) {
                    std::error_code pec;
                    const auto resolved_proj =
                        std::filesystem::weakly_canonical(
                            (manifest_path.parent_path() / proj->front())
                                .lexically_normal(),
                            pec);
                    const auto expected_proj =
                        std::filesystem::weakly_canonical(
                            expected_project_root.lexically_normal(), pec);
                    if (pec || resolved_proj != expected_proj) {
                        error = manifest_path.string()
                                + ": project: does not resolve to"
                                  " expected project root";
                        return false;
                    }
                }
            } else {
                if (proj != nullptr) {
                    error = manifest_path.string()
                            + ": unexpected project: declaration";
                    return false;
                }
            }
        } else if (lookup(doc, "project") != nullptr) {
            error = manifest_path.string()
                    + ": unexpected project: declaration";
            return false;
        }
        const auto* doc_folders = lookup(doc, "folder");
        std::set<std::string> doc_f_set;
        bool folder_mismatch = false;
        if (doc_folders != nullptr) {
            for (const auto& f : *doc_folders) {
                if (!doc_f_set.insert(f).second) {
                    if (!error.empty()) error += "\n";
                    error += manifest_path.string()
                             + ": duplicate folder: " + f;
                    folder_mismatch = true;
                }
            }
        }
        std::set<std::string> exp_f_set(dir_node->folders.begin(),
                                        dir_node->folders.end());
        for (const auto& exp : exp_f_set) {
            if (doc_f_set.find(exp) == doc_f_set.end()) {
                if (!error.empty()) error += "\n";
                error += manifest_path.string() + " does not name " + exp
                        + "; add folder: " + exp;
                folder_mismatch = true;
            }
        }
        for (const auto& got : doc_f_set) {
            if (exp_f_set.find(got) == exp_f_set.end()) {
                if (!error.empty()) error += "\n";
                error += manifest_path.string() + ": unexpected folder: "
                        + got;
                folder_mismatch = true;
            }
        }
        if (folder_mismatch) return false;
        return true;
    }

    if (app_node != nullptr) {
        const auto* kind = lookup(doc, "kind");
        if (kind == nullptr || kind->size() != 1 ||
            kind->front() != "app") {
            error = manifest_path.string() + ": expected kind: app";
            return false;
        }
        const auto* name = lookup(doc, "name");
        if (name == nullptr || name->size() != 1 ||
            name->front() != app_node->name) {
            error = manifest_path.string() + ": expected name: "
                    + app_node->name;
            return false;
        }
        if (lookup(doc, "project") != nullptr) {
            error = manifest_path.string()
                    + ": unexpected project: declaration";
            return false;
        }
        const auto* use = lookup(doc, "use");
        const auto sketch_uses =
            use == nullptr ? 0 : std::count(use->begin(), use->end(),
                                            "mm.sketch");
        if (sketch_uses != 1) {
            error = manifest_path.string() + ": expected use: mm.sketch";
            return false;
        }
        const auto* file = lookup(doc, "file");
        const auto main_files =
            file == nullptr ? 0 : std::count(file->begin(), file->end(),
                                             "main.cpp");
        if (main_files != 1) {
            error = manifest_path.string() + ": expected file: main.cpp";
            return false;
        }
        const auto* sketches = lookup(doc, "sketch");
        if (sketches == nullptr || *sketches != app_node->sketches) {
            error = manifest_path.string()
                    + ": sketch: entries do not match discovered sketches";
            return false;
        }
        const auto* lib = lookup(doc, "sketch-library");
        if (lib == nullptr || lib->size() != 1 || lib->front().empty()) {
            error = manifest_path.string()
                    + ": expected one sketch-library: declaration";
            return false;
        }
        const std::filesystem::path raw_library(lib->front());
        if (raw_library.is_absolute()) {
            error = manifest_path.string()
                    + ": sketch-library: value must be relative";
            return false;
        }
        std::error_code ec;
        const auto resolved_lib = std::filesystem::weakly_canonical(
            (app_node->dir / lib->front()).lexically_normal(), ec);
        const auto resolved_expected = std::filesystem::weakly_canonical(
            library_root.lexically_normal(), ec);
        if (ec || resolved_lib != resolved_expected) {
            error = manifest_path.string()
                    + ": sketch-library does not resolve to library root";
            return false;
        }
        return true;
    }

    return false;
}

} // namespace mm::ino
