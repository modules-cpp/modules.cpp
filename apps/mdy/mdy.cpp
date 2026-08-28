// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <map>
#include <format>

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

// C++20 vector formatter
template <>
struct std::formatter<std::vector<std::string>> : std::formatter<std::string> {
    auto format(const std::vector<std::string>& v, std::format_context& ctx) const {
        auto out = ctx.out();
        bool first = true;
        for (const auto& s : v) {
            if (!first) out = std::format_to(out, ", ");
            out = std::format_to(out, "{}", s);
            first = false;
        }
        return out;
    }
};

int sample() {
    std::filesystem::path mdy_file = "./out/sample.mdy";
    create_dummy_mdy(mdy_file);

    // Run our modular front matter parser
    mm::mdy::MDYDocument doc = mm::mdy::Parser::parse_file(mdy_file);

    // Print parsed front matter metadata
    std::cout << "=== METADATA EXTRACTED ===\n";
    for (const auto& [key, values] : doc.metadata) {
        for (const auto& val : values) {
            std::cout << key << " " << val << "\n";
        }
    }

    std::cout << "\n=== BODY CONTENT TRAVERSAL ===\n";
    for (const auto& block : doc.body) {
        if (block.type == mm::mdy::BlockType::Heading1) {
            std::cout << "Heading1: " << block.content << "\n";
        } else if (block.type == mm::mdy::BlockType::Heading2) {
            std::cout << "Heading2: " << block.content << "\n";
        } else if (block.type == mm::mdy::BlockType::Heading3) {
            std::cout << "Heading3: " << block.content << "\n";
        } else if (block.type == mm::mdy::BlockType::UnorderedList) {
            std::cout << "UnorderedList: " << block.content << "\n";
        } else {
            std::cout << "Text: " << block.content << "\n";
        }
    }

    return 0;
}



// read mdy file 
int main(int argc, char** argv) {
    bool verbose = false;
    bool html = false;

    if (argc < 2) {
        std::cout << "usage: [-s] | file.mdy [-v, -h]" << "\n!";
        exit(1);
    }

    if (argc == 2) {
        if(std::string(argv[1]) == std::string("-s"))
        {
            verbose = true;
            exit(sample());
        }
    }

    if (argc == 3) {
        if(std::string(argv[2]) == std::string("-v"))
            verbose = true;
    }
  
    if (argc == 3) {
        if(std::string(argv[2]) == std::string("-h"))
            html = true;
    }

    if (verbose || html)
        std::cout << "mdy file: " << argv[1] << "\n";

    std::filesystem::path mdy_file = argv[1];
    
    if (!std::filesystem::exists(mdy_file)) {
        std::cerr << "file not found " << mdy_file << "\n";
        exit(1);
    }
    // Run our modular front matter parser
    mm::mdy::MDYDocument doc = mm::mdy::Parser::parse_file(mdy_file);

    if (verbose) {
        // Print parsed front matter metadata
        std::cout << "--- metadata --\n";
        for (const auto& [key, values] : doc.metadata) {
            for (const auto& val : values) {
                std::cout << key << " " << val << "\n";
            }
        }

        std::cout << "\n=== mdy content ===\n";
        for (const auto& block : doc.body) {
            if (block.type == mm::mdy::BlockType::Heading1) {
                std::cout << "H1: " << block.content << "\n";
            } else if (block.type == mm::mdy::BlockType::Heading2) {
                std::cout << "H2: " << block.content << "\n";
            } else if (block.type == mm::mdy::BlockType::Heading3) {
                std::cout << "H3: " << block.content << "\n";
            } else if (block.type == mm::mdy::BlockType::UnorderedList) {
                std::cout << "UL: " << block.content << "\n";
            } else {
                std::cout << "TX: " << block.content << "\n";
            }
        }
    }

    if (html) {
        std::cout << to_html(doc.body);
    }

    return 0;
}
