#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.mdy;
import mm.test;

namespace {

using mm::mdy::BlockType;
using mm::mdy::MDYDocument;
using mm::mdy::Parser;


MDYDocument parse_text(std::string_view text)
{
    const mm::test::scoped_file file{
        "/tmp/~modueles.cpp_mm_mdy_test_integration.mdy",
        text
    };
    return Parser::parse_file(file.path());
}

std::size_t value_count(const MDYDocument& doc, std::string_view key) {
    const auto it = doc.metadata.find(key);
    return it == doc.metadata.end() ? 0 : it->second.size();
}

std::string first_value(const MDYDocument& doc, std::string_view key) {
    const auto it = doc.metadata.find(key);
    return it == doc.metadata.end() || it->second.empty() ? std::string{} : it->second.front();
}

// --- front matter -------------------------------------------------------

void reads_front_matter_and_body() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "kind: file\n"
        "name: sample.mdy\n"
        "---\n"
        "# Title\n"
        "- item\n");

    mm::test::expect(doc.metadata.size() == 3, "expected three front matter keys");
    mm::test::expect(first_value(doc, "mm") == "1.0", "expected mm to be 1.0");
    mm::test::expect(first_value(doc, "kind") == "file", "expected kind to be file");
    mm::test::expect(first_value(doc, "name") == "sample.mdy", "expected name to be sample.mdy");

    mm::test::expect(doc.body.size() == 2, "expected two body blocks");
    mm::test::expect(doc.body[0].type == BlockType::Heading1, "expected first body block to be Heading1");
    mm::test::expect(doc.body[1].type == BlockType::UnorderedList, "expected second body block to be a list item");
}

void repeated_keys_accumulate() {
    const auto doc = parse_text(
        "---\n"
        "tag: a\n"
        "tag: b\n"
        "---\n");

    mm::test::expect(value_count(doc, "tag") == 2, "expected a repeated key to collect both values");
    mm::test::expect(first_value(doc, "tag") == "a", "expected values to keep document order");
}

void strips_surrounding_quotes() {
    const auto doc = parse_text(
        "---\n"
        "name: \"My Title\"\n"
        "---\n");

    mm::test::expect(first_value(doc, "name") == "My Title",
                     "expected surrounding quotes to be stripped without touching the text");
}

void leaves_lone_quote_alone() {
    const auto doc = parse_text(
        "---\n"
        "name: \"\n"
        "---\n");

    mm::test::expect(first_value(doc, "name") == "\"",
                     "expected a single quote character to survive quote stripping");
}

void keeps_colon_inside_value() {
    const auto doc = parse_text(
        "---\n"
        "note: colon: inside\n"
        "---\n");

    mm::test::expect(first_value(doc, "note") == "colon: inside",
                     "expected only the first colon to separate key from value");
}

void ignores_front_matter_line_without_colon() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "bare line\n"
        "---\n");

    mm::test::expect(doc.metadata.size() == 1, "expected a line without a colon to be skipped");
}

// --- body ---------------------------------------------------------------

void document_without_front_matter_is_all_body() {
    const auto doc = parse_text("# Hello\ntext\n");

    mm::test::expect(doc.metadata.empty(), "expected no metadata when the file has no front matter");
    mm::test::expect(doc.body.size() == 2, "expected both lines to become body blocks");
    mm::test::expect(doc.body[0].type == BlockType::Heading1, "expected the heading to parse");
}

void fence_in_body_is_not_front_matter() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "before\n"
        "---\n"
        "after\n");

    mm::test::expect(doc.metadata.size() == 1, "expected a later --- not to reopen front matter");
    mm::test::expect(doc.body.size() == 3, "expected the body fence to be kept as content");
}

void blank_body_lines_are_skipped() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "one\n"
        "\n"
        "two\n");

    mm::test::expect(doc.body.size() == 2, "expected blank spacer lines to be dropped from the body");
}

void handles_crlf_line_endings() {
    const auto doc = parse_text(
        "---\r\n"
        "mm: 1.0\r\n"
        "---\r\n"
        "# Title\r\n");

    mm::test::expect(first_value(doc, "mm") == "1.0", "expected CRLF front matter to parse");
    mm::test::expect(doc.body.size() == 1, "expected one body block from a CRLF file");
    mm::test::expect(doc.body[0].content == "Title", "expected no stray carriage return in content");
}

void empty_input_yields_empty_document() {
    const auto doc = parse_text("");

    mm::test::expect(doc.metadata.empty(), "expected no metadata from empty input");
    mm::test::expect(doc.body.empty(), "expected no body from empty input");
}

// --- file entry points --------------------------------------------------

void parse_file_reads_from_disk() {
    const mm::test::scoped_file file{"mm_mdy_test_basic.mdy",
        "---\n"
        "mm: 1.0\n"
        "kind: file\n"
        "---\n"
        "# Title\n"};

    const auto doc = Parser::parse_file(file.path());

    mm::test::expect(doc.metadata.size() == 2, "expected front matter to be read from disk");
    mm::test::expect(doc.body.size() == 1, "expected the body to be read from disk");
}

void parse_file_of_missing_path_is_empty() {
    const auto missing = std::filesystem::temp_directory_path() / "mm_mdy_test_does_not_exist.mdy";
    const auto doc = Parser::parse_file(missing);

    mm::test::expect(doc.metadata.empty(), "expected a missing file to produce no metadata");
    mm::test::expect(doc.body.empty(), "expected a missing file to produce no body");
}

void parse_file_of_directory_is_empty() {
    const auto doc = Parser::parse_file(std::filesystem::temp_directory_path());

    mm::test::expect(doc.metadata.empty(), "expected a directory path to produce no metadata");
    mm::test::expect(doc.body.empty(), "expected a directory path to produce no body");
}

void parse_returns_every_line_as_a_block() {
    const mm::test::scoped_file file{"mm_mdy_test_parse.mdy", "# Title\n- item\nprose\n"};

    const auto blocks = Parser::parse(file.path());

    mm::test::expect(blocks.size() == 3, "expected Parser::parse to return one block per line");
    mm::test::expect(blocks[0].type == BlockType::Heading1, "expected the heading to parse");
}

const mm::test::case_ cases[] = {
    { "reads front matter and body",              &reads_front_matter_and_body },
    { "repeated keys accumulate",                 &repeated_keys_accumulate },
    { "strips surrounding quotes",                &strips_surrounding_quotes },
    { "leaves lone quote alone",                  &leaves_lone_quote_alone },
    { "keeps colon inside value",                 &keeps_colon_inside_value },
    { "ignores front matter line without colon",  &ignores_front_matter_line_without_colon },
    { "document without front matter is body",    &document_without_front_matter_is_all_body },
    { "fence in body is not front matter",        &fence_in_body_is_not_front_matter },
    { "blank body lines are skipped",             &blank_body_lines_are_skipped },
    { "handles CRLF line endings",                &handles_crlf_line_endings },
    { "empty input yields empty document",        &empty_input_yields_empty_document },
    { "parse_file reads from disk",               &parse_file_reads_from_disk },
    { "parse_file of missing path is empty",      &parse_file_of_missing_path_is_empty },
    { "parse_file of directory is empty",         &parse_file_of_directory_is_empty },
    { "parse returns every line as a block",      &parse_returns_every_line_as_a_block },
};

const mm::test::registrar reg{"mm.mdy integration", cases};

}
