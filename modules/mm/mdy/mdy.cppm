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

// Holds both the front matter metadata and the body content
export struct MDYDocument {
    std::map<std::string, std::vector<std::string>, std::less<>> metadata;
    std::vector<Block> body;
};

// Export the primary API functions
export class Parser {
public:
    // Parse an explicit file path using C++20 file system components
    [[nodiscard]] static std::vector<Block> parse(const std::filesystem::path& file_path);
    [[nodiscard]] static MDYDocument parse_file(const std::filesystem::path& file_path);
};

}