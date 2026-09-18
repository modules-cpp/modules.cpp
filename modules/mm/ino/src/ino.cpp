// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
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

        for (std::size_t line_idx = 0; line_idx < pf.lines.size(); ++line_idx) {
            const std::string& line = pf.lines[line_idx];
            const std::size_t line_num = line_idx + 1;

            std::string_view trimmed = trim_leading(line);

            // Check if this line is an include directive (must be at top level outside block comment)
            if (!in_block_comment && trimmed.starts_with("#include")) {
                std::string_view inc_rest = trim_leading(trimmed.substr(8));
                if (inc_rest.starts_with("\"")) {
                    result.ok = false;
                    result.diagnostics.push_back({
                        src.path,
                        line_num,
                        "quoted include not allowed: " + std::string(trimmed),
                        false
                    });
                } else if (inc_rest.starts_with("<")) {
                    std::size_t close = inc_rest.find('>');
                    if (close != std::string_view::npos) {
                        std::string header = std::string(inc_rest.substr(1, close - 1));
                        if (std::find(hoisted_includes.begin(), hoisted_includes.end(), header) == hoisted_includes.end()) {
                            hoisted_includes.push_back(header);
                        }
                        pf.skip_line[line_idx] = true;
                    }
                }
            }

            // If we are at brace depth 0 and not in block comment, check for definitions / prototypes
            if (brace_depth == 0 && !in_block_comment) {
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
                            // If a generated prototype for this already exists, remove it
                            auto it = std::remove_if(generated_prototypes.begin(), generated_prototypes.end(),
                                                     [&fn_name](const std::string& p) {
                                                         return p.find(" " + fn_name + "(") != std::string::npos;
                                                     });
                            generated_prototypes.erase(it, generated_prototypes.end());
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
        out += "#include <" + inc + ">\n";
    }

    // 3. Prelude
    out += "import mm.sketch;\n";
    out += "using namespace mm::sketch;\n";

    // 4. Prototypes
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

    // 5. Sketch text file by file
    for (const auto& pf : processed_files) {
        std::size_t current_lines = count_newlines(out);
        std::size_t displacement = current_lines + 1;
        out += "// " + std::filesystem::path(pf.path).filename().string() +
               " (displaced by " + std::to_string(displacement) + " lines)\n";
        for (std::size_t i = 0; i < pf.lines.size(); ++i) {
            if (pf.skip_line[i]) continue;
            out += pf.lines[i] + "\n";
        }
    }

    // 6. Main
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

    return true;
}

} // namespace mm::ino
