// Regression tests for known mm.mdy parser defects.
//
// Every case in this file currently FAILS. Each one pins behaviour the parser
// is supposed to have; delete a case only when the corresponding defect is
// genuinely fixed, never to make the suite green.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

import mm.mdy;
import mm.test;

namespace {

using mm::mdy::BlockType;
using mm::mdy::MDYDocument;
using mm::mdy::Parser;

// Writes text to a uniquely named file under the system temp directory and
// removes it again when the test case leaves scope.
class scoped_file {
public:
    scoped_file(std::string_view name, std::string_view text)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::ofstream out(path_, std::ios::binary);
        out << text;
    }

    ~scoped_file() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    scoped_file(const scoped_file&) = delete;
    scoped_file& operator=(const scoped_file&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

MDYDocument parse_text(std::string_view text)
{
    const scoped_file file{
        "/tmp/~modueles.cpp_mm_mdy_test_defect.mdy",
        text
    };
    return Parser::parse_file(file.path());
}

// Defect: a YAML block sequence stores one empty string under the key and
// silently drops every '- item' line, so the vector value type can never hold
// more than one entry except through a repeated key.
void block_sequence_collects_values() {
    const auto doc = parse_text(
        "---\n"
        "tags:\n"
        "  - cpp\n"
        "  - modules\n"
        "---\n");

    const auto it = doc.metadata.find("tags");
    mm::test::expect(it != doc.metadata.end(), "expected a tags key");
    mm::test::expect(it->second.size() == 2, "expected a block sequence to collect both items");
    mm::test::expect(it->second[0] == "cpp", "expected the first sequence item to be cpp");
    mm::test::expect(it->second[1] == "modules", "expected the second sequence item to be modules");
}

// Defect: front matter that is never closed swallows the whole file. Every
// remaining line is treated as metadata and the body comes back empty, which a
// caller cannot tell apart from a document that simply has no body.
void unterminated_front_matter_keeps_body() {
    const auto doc = parse_text(
        "---\n"
        "mm: 0.1\n"
        "# not metadata\n");

    mm::test::expect(doc.body.size() == 1,
                     "expected content after unterminated front matter to stay in the body");
}

// Defect: first_line is cleared by any first line, including a blank one, so a
// leading empty line stops the opening fence from being recognised and the
// whole front matter block is parsed as body text.
void blank_line_before_fence_still_opens_front_matter() {
    const auto doc = parse_text(
        "\n"
        "---\n"
        "mm: 0.1\n"
        "---\n"
        "# Title\n");

    mm::test::expect(doc.metadata.size() == 1, "expected front matter after a leading blank line");
    mm::test::expect(doc.body.size() == 1, "expected only the heading in the body");
}

// Defect: body lines are trimmed before block parsing, so nested list items are
// indistinguishable from top level ones and indentation can never reach a
// renderer.
void body_keeps_indentation() {
    const auto doc = parse_text(
        "---\n"
        "mm: 0.1\n"
        "---\n"
        "- item\n"
        "  - item\n");

    mm::test::expect(doc.body.size() == 2, "expected both list items");

    // Same text at two nesting depths currently produces identical blocks:
    // indentation never reaches the parsed representation at all.
    const bool distinguishable = doc.body[0].content != doc.body[1].content ||
                                 doc.body[0].type != doc.body[1].type;
    mm::test::expect(distinguishable,
                     "expected a nested list item to differ from a top level item with the same text");
}

const mm::test::case_ cases[] = {
    { "block sequence collects values",                 &block_sequence_collects_values, true },
    { "unterminated front matter keeps body",           &unterminated_front_matter_keeps_body, true },
    { "blank line before fence opens front matter",     &blank_line_before_fence_still_opens_front_matter, true },
    { "body keeps indentation",                         &body_keeps_indentation, true },
};

const mm::test::registrar reg{"mm.mdy known defects", cases};

}
