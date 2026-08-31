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
export module mm.mdy;

namespace mm::mdy {


// Export the enum so users of the module can check block types
export enum class BlockType {
    Empty,
    Heading1,
    Heading2,
    Heading3,
    Paragraph,
    UnorderedList
};

// Export the structure holding parsed tokens
export struct Block {
    BlockType type;
    std::string content;
};

// Whether parse_file actually read a file, distinct from whether what it
// read happened to be empty: an empty file is Ok (a legitimately empty
// document, no different from "---\n---\n"), while NotFound and Unreadable
// are I/O failures parse_file previously could not report at all, since it
// always returned a default constructed, empty MDYDocument for every one
// of these cases indistinguishably from a genuinely empty document.
export enum class ParseStatus {
    Ok,
    NotFound,     // file_path does not exist
    Unreadable,   // file_path exists but could not be opened for reading
};

// Holds both the front matter metadata and the body content
export struct MDYDocument {
    std::map<std::string, std::vector<std::string>, std::less<>> metadata;
    std::vector<Block> body;
    ParseStatus status = ParseStatus::Ok;
};

// Internal module API used by the implementation and white-box tests.
// These are not exported to ordinary importers.
std::string_view trim(std::string_view text);
Block parse_line(std::string_view line);


// Export the primary API functions
export class Parser {
public:
    // Parse an explicit file path using C++20 file system components
    [[nodiscard]] static std::vector<Block> parse(const std::filesystem::path& file_path);
    [[nodiscard]] static MDYDocument parse_file(const std::filesystem::path& file_path);
};

}