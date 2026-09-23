// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.posix;

import :compatibility;

namespace mm::shell {

namespace {

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string strip_quotes(std::string_view value) {
    if (value.size() >= 2) {
        const char front = value.front();
        const char back = value.back();
        if ((front == '"' && back == '"') || (front == '\'' && back == '\''))
            return std::string(value.substr(1, value.size() - 2));
    }
    return std::string(value);
}

}  // namespace

std::vector<ScriptLine> parse_script(const std::filesystem::path& path) {
    std::vector<ScriptLine> lines;
    std::ifstream file(path);
    std::string raw;

    while (std::getline(file, raw)) {
        ScriptLine line;
        line.text = raw;

        const auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) {
            line.kind = LineKind::Empty;
            lines.push_back(std::move(line));
            continue;
        }
        if (raw[first] == '#') {
            line.kind = LineKind::Comment;
            lines.push_back(std::move(line));
            continue;
        }

        if (is_identifier_start(raw[first])) {
            std::size_t name_end = first;
            while (name_end < raw.size() && is_identifier_char(raw[name_end])) {
                ++name_end;
            }
            if (name_end < raw.size() && raw[name_end] == '=') {
                line.kind = LineKind::Assignment;
                line.name = raw.substr(first, name_end - first);
                line.value = strip_quotes(raw.substr(name_end + 1));
                lines.push_back(std::move(line));
                continue;
            }
        }

        line.kind = LineKind::Command;
        lines.push_back(std::move(line));
    }

    return lines;
}

}  // namespace mm::shell
