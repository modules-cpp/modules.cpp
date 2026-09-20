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

// Reads every line of file into lines. False when the file cannot be opened
// or read: a directory opens fine but fails on the first read, setting
// badbit rather than the eofbit a normal, possibly empty, file ends with.
bool read_lines(const std::filesystem::path& file, std::vector<std::string>& lines) {
    std::ifstream in(file);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) lines.push_back(std::move(line));
    if (in.bad()) return false;
    return true;
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

// Append one body line to the accumulated body. A plain line joins the
// pending paragraph with a single space; a heading or list line ends the
// pending paragraph before emitting its own block; an empty line ends the
// pending paragraph and adds nothing. The caller flushes whatever paragraph
// is still pending at end of input.
void append_body_line(std::vector<Block>& body, std::string& pending_paragraph,
                      std::string_view line_view) {
    if (line_view.empty()) {
        if (!pending_paragraph.empty()) {
            body.push_back({BlockType::Paragraph, std::move(pending_paragraph)});
        }
        return;
    }
    Block block = parse_line(line_view);
    if (block.type == BlockType::Paragraph) {
        if (!pending_paragraph.empty()) pending_paragraph += " ";
        pending_paragraph += block.content;
        return;
    }
    if (!pending_paragraph.empty()) {
        body.push_back({BlockType::Paragraph, std::move(pending_paragraph)});
    }
    body.push_back(std::move(block));
}

// Implement the exported static method to parse files
std::vector<Block> Parser::parse(const std::filesystem::path& file_path) {
    std::vector<Block> parsed_blocks;
    
    if (!std::filesystem::exists(file_path)) {
        return parsed_blocks;
    }

    std::vector<std::string> lines;
    if (!read_lines(file_path, lines)) return parsed_blocks;
    std::string pending_paragraph;
    for (const auto& line : lines) {
        append_body_line(parsed_blocks, pending_paragraph, trim(line));
    }
    if (!pending_paragraph.empty()) {
        parsed_blocks.push_back({BlockType::Paragraph, std::move(pending_paragraph)});
    }

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

    std::vector<std::string> lines;
    if (!read_lines(file_path, lines)) {
        doc.status = ParseStatus::Unreadable;
        return doc;
    }

    // State machine states
    enum class ParseState { ExpectingStartFence, InsideFrontMatter, InsideBody };
    ParseState state = ParseState::ExpectingStartFence;

    bool first_line = true;
    std::string pending_paragraph;

    for (const auto& line : lines) {
        std::string_view line_view = trim(line);

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
            // We are in the body. Empty lines end the pending paragraph;
            // plain lines join it; heading and list lines end it first.
            append_body_line(doc.body, pending_paragraph, line_view);
        }
    }

    if (!pending_paragraph.empty()) {
        doc.body.push_back({BlockType::Paragraph, std::move(pending_paragraph)});
    }

    return doc;
}

}

namespace mm::mdy {

// Unified manifest lookup: one front-matter key's values, its first value,
// or every value, or nullptr for a key the document does not carry.
const std::vector<std::string>* lookup(const MDYDocument& doc, std::string_view key) {
    auto it = doc.metadata.find(key);
    return it == doc.metadata.end() ? nullptr : &it->second;
}

std::string first(const MDYDocument& doc, std::string_view key) {
    const auto* values = lookup(doc, key);
    return values == nullptr || values->empty() ? std::string{} : values->front();
}

std::vector<std::string> all(const MDYDocument& doc, std::string_view key) {
    const auto* values = lookup(doc, key);
    return values == nullptr ? std::vector<std::string>{} : *values;
}

}  // namespace mm::mdy
