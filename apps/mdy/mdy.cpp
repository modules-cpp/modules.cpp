// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <map>

import mm.mdy;

void create_dummy_mdy(const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "---\n";
    out << "mm: 0.1\n";
    out << "kind: file\n";
    out << "name: sample.mdy\n";
    out << "---\n";
    out << "# modules.cpp C++20 Modules\n";
    out << "## Features\n";
    out << "Faster compilation speeds than headers\n";
    out << "True logical separation of interface code\n";
    out << "### Rules\n";
    out << "Modules replace old header-file macro include frameworks entirely.\n";
}


// Implement  static HTML renderer
std::string to_html(const std::vector<mm::mdy::Block>& blocks) {
    std::string html_output;
    
    for (const auto& block : blocks) {
        switch (block.type) {
            case mm::mdy::BlockType::Heading1:
                html_output += "<h1>" + block.content + "</h1>\n";
                break;
            case mm::mdy::BlockType::Heading2:
                html_output += "<h2>" + block.content + "</h2>\n";
                break;
            case mm::mdy::BlockType::Heading3:
                html_output += "<h3>" + block.content + "</h3>\n";
                break;
            case mm::mdy::BlockType::UnorderedList:
                html_output += "  <li>" + block.content + "</li>\n";
                break;
            case mm::mdy::BlockType::Paragraph:
                html_output += "<p>" + block.content + "</p>\n";
                break;
        }
    }
    return html_output;
}

int main() {
    std::filesystem::path mdy_file = "./out/sample.mdy";
    create_dummy_mdy(mdy_file);

    // Run our modular front matter parser
    mm::mdy::MDYDocument doc = mm::mdy::Parser::parse_file(mdy_file);

    // Print parsed front matter metadata
    std::cout << "=== METADATA EXTRACTED ===\n";
    for (const auto& [key, value] : doc.metadata) {
        std::cout << key << " -> " << value << "\n";
    }

    std::cout << "\n=== BODY CONTENT TRAVERSAL ===\n";
    for (const auto& block : doc.body) {
        if (block.type == mm::mdy::BlockType::Heading1) {
            std::cout << "Heading1: " << block.content << "\n";
        } else if (block.type == mm::mdy::BlockType::Heading2) {
            std::cout << "Heading2: " << block.content << "\n";
        } else if (block.type == mm::mdy::BlockType::Heading3) {
            std::cout << "Heading3: " << block.content << "\n";
        } else {
            std::cout << "Text: " << block.content << "\n";
        }
    }

    return 0;
}
