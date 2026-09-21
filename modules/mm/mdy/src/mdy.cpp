// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <filesystem>

module mm.mdy;

namespace mm::mdy {
// Internal helper to strip leading and trailing whitespace from string_views
std::string_view trim(std::string_view text)
{
    constexpr std::string_view whitespace = " \t\r\n\f\v";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

// True when the line starts a heading or list block instead of
// continuing a paragraph run.
bool starts_block(std::string_view line) {
    return line.starts_with("# ") || line.starts_with("## ") ||
           line.starts_with("### ") || line.starts_with("- ") ||
           line.starts_with("* ");
}

// Emit the pending paragraph run, if any.
void flush_paragraph(std::vector<Block>& blocks, std::string& pending) {
    if (!pending.empty()) {
        blocks.push_back({BlockType::Paragraph, std::move(pending)});
        pending.clear();
    }
}

// C++20 parsing helper function
Block parse_line(std::string_view line) {
    // 1. Trim leading space if necessary (simplified)
    if (line.empty()) {
        return {BlockType::Empty, ""};
    }

    // 2. Leverage C++20 string_view extensions (.starts_with)
    if (line.starts_with("# ")) {
        return {BlockType::Heading1, std::string(line.substr(2))};
    } 
    if (line.starts_with("## ")) {
        return {BlockType::Heading2, std::string(line.substr(3))};
    } 
    if (line.starts_with("### ")) {
        return {BlockType::Heading3, std::string(line.substr(4))};
    } 
    if (line.starts_with("- ") || line.starts_with("* ")) {
        return {BlockType::UnorderedList, std::string(line.substr(2))};
    }

    // Default to a standard paragraph
    return {BlockType::Paragraph, std::string(line)};
}

// Implement the exported static method to parse files
std::vector<Block> Parser::parse(const std::filesystem::path& file_path) {
    std::vector<Block> parsed_blocks;
    
    if (!std::filesystem::exists(file_path)) {
        return parsed_blocks;
    }

    std::ifstream file(file_path);
    std::string current_line;
    std::string pending;

    while (std::getline(file, current_line)) {
        std::string_view view(current_line);

        // Empty lines separate paragraph runs gracefully
        if (view.empty()) {
            flush_paragraph(parsed_blocks, pending);
            continue;
        }

        if (starts_block(view)) {
            flush_paragraph(parsed_blocks, pending);
            parsed_blocks.push_back(parse_line(view));
            continue;
        }

        if (pending.empty()) pending = std::string(view);
        else pending += " " + std::string(view);
    }

    flush_paragraph(parsed_blocks, pending);

    return parsed_blocks;
}


MDYDocument Parser::parse_file(const std::filesystem::path& file_path) {
    MDYDocument doc;

    std::error_code ec;
    const bool exists = std::filesystem::exists(file_path, ec);
    if (ec || !exists) {
        doc.status = ParseStatus::NotFound;
        return doc;
    }

    if (std::filesystem::is_directory(file_path, ec)) {
        doc.status = ParseStatus::Unreadable;
        return doc;
    }
    if (ec) {
        doc.status = ParseStatus::Unreadable;
        return doc;
    }

    std::ifstream file(file_path);
    if (!file.is_open()) {
        doc.status = ParseStatus::Unreadable;
        return doc;
    }

    std::string current_line;
    std::string pending;

    // State machine states
    enum class ParseState { ExpectingStartFence, InsideFrontMatter, InsideBody };
    ParseState state = ParseState::ExpectingStartFence;

    bool first_line = true;

    while (std::getline(file, current_line)) {
        std::string_view line_view = trim(current_line);

        // 1. Check for YAML boundary markers (---)
        if (line_view == "---") {
            if (first_line && state == ParseState::ExpectingStartFence) {
                state = ParseState::InsideFrontMatter;
                first_line = false;
                continue;
            } 
            if (state == ParseState::InsideFrontMatter) {
                state = ParseState::InsideBody;
                continue;
            }
        }

        first_line = false;

        // 2. Handle parsing based on the current state
        if (state == ParseState::InsideFrontMatter) {
            auto colon_pos = line_view.find(':');
            if (colon_pos != std::string_view::npos) {
                std::string_view key = trim(line_view.substr(0, colon_pos));
                std::string_view value = trim(line_view.substr(colon_pos + 1));
                
                // Optional: Strip quotes from values if present (e.g., "My Title")
                if (value.starts_with('"') && value.ends_with('"') && value.size() >= 2) {
                    value = value.substr(1, value.size() - 2);
                }

                doc.metadata[std::string(key)].push_back(std::string(value));
            }
        }
        else {
            // Body: plain lines accumulate into a paragraph run; headings and
            // list items end the run; empty lines separate runs.
            if (line_view.empty()) {
                flush_paragraph(doc.body, pending);
                continue;
            }
            if (starts_block(line_view)) {
                flush_paragraph(doc.body, pending);
                doc.body.push_back(parse_line(line_view));
                continue;
            }
            if (pending.empty()) pending = std::string(line_view);
            else pending += " " + std::string(line_view);
        }
    }

    flush_paragraph(doc.body, pending);

    // is_open() is true for a path that opens but cannot actually be read,
    // such as a directory (POSIX open() on a directory succeeds; the
    // failure shows up on the first read instead, setting badbit rather
    // than the eofbit a normal, possibly empty, file ends the loop with).
    if (file.bad()) doc.status = ParseStatus::Unreadable;

    return doc;
}

}
