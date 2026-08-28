module;

#include <string>
#include <string_view>

module mm.mdy;

import mm.test;

namespace {

using mm::mdy::BlockType;
using mm::mdy::parse_line;
using mm::mdy::trim;

// --- trim ---------------------------------------------------------------

void trim_removes_outer_whitespace() {
    mm::test::expect(trim("  value  ") == "value",
                     "expected outer whitespace to be removed");
}

void trim_preserves_interior_whitespace() {
    mm::test::expect(trim("  My Title  ") == "My Title",
                     "expected interior whitespace to survive trimming");
}

void trim_of_blank_is_empty() {
    mm::test::expect(trim("   \t  ").empty(), "expected blank input to trim to empty");
    mm::test::expect(trim("").empty(), "expected empty input to trim to empty");
}

void trim_removes_carriage_return() {
    mm::test::expect(trim("value\r") == "value",
                     "expected trailing CR to be removed so CRLF files parse");
}

void trim_leaves_clean_input_alone() {
    mm::test::expect(trim("value") == "value", "expected untouched input to be returned as is");
}

// --- parse_line ---------------------------------------------------------

void parse_line_reads_heading_levels() {
    const auto h1 = parse_line("# Title");
    mm::test::expect(h1.type == BlockType::Heading1, "expected '# ' to yield Heading1");
    mm::test::expect(h1.content == "Title", "expected Heading1 marker to be stripped");

    const auto h2 = parse_line("## Features");
    mm::test::expect(h2.type == BlockType::Heading2, "expected '## ' to yield Heading2");
    mm::test::expect(h2.content == "Features", "expected Heading2 marker to be stripped");

    const auto h3 = parse_line("### Detail");
    mm::test::expect(h3.type == BlockType::Heading3, "expected '### ' to yield Heading3");
    mm::test::expect(h3.content == "Detail", "expected Heading3 marker to be stripped");
}

void parse_line_stops_at_heading_three() {
    const auto block = parse_line("#### Deep");
    mm::test::expect(block.type == BlockType::Paragraph,
                     "expected level 4 headings to fall back to Paragraph");
}

void parse_line_requires_space_after_hash() {
    const auto block = parse_line("#NoSpace");
    mm::test::expect(block.type == BlockType::Paragraph,
                     "expected a hash without a space to be a Paragraph");
}

void parse_line_reads_both_list_markers() {
    const auto dash = parse_line("- dash item");
    mm::test::expect(dash.type == BlockType::UnorderedList, "expected '- ' to yield UnorderedList");
    mm::test::expect(dash.content == "dash item", "expected list marker to be stripped");

    const auto star = parse_line("* star item");
    mm::test::expect(star.type == BlockType::UnorderedList, "expected '* ' to yield UnorderedList");
    mm::test::expect(star.content == "star item", "expected list marker to be stripped");
}

void parse_line_reads_empty_line() {
    const auto block = parse_line("");
    mm::test::expect(block.type == BlockType::Empty, "expected an empty line to yield Empty");
    mm::test::expect(block.content.empty(), "expected an Empty block to carry no content");
}

void parse_line_defaults_to_paragraph() {
    const auto block = parse_line("just some prose");
    mm::test::expect(block.type == BlockType::Paragraph, "expected plain text to yield Paragraph");
    mm::test::expect(block.content == "just some prose", "expected paragraph text to be kept whole");
}

const mm::test::case_ cases[] = {
    { "trim removes outer whitespace",       &trim_removes_outer_whitespace },
    { "trim preserves interior whitespace",  &trim_preserves_interior_whitespace },
    { "trim of blank input is empty",        &trim_of_blank_is_empty },
    { "trim removes carriage return",        &trim_removes_carriage_return },
    { "trim leaves clean input alone",       &trim_leaves_clean_input_alone },
    { "parse_line reads heading levels",     &parse_line_reads_heading_levels },
    { "parse_line stops at heading three",   &parse_line_stops_at_heading_three },
    { "parse_line requires space after #",   &parse_line_requires_space_after_hash },
    { "parse_line reads both list markers",  &parse_line_reads_both_list_markers },
    { "parse_line reads empty line",         &parse_line_reads_empty_line },
    { "parse_line defaults to paragraph",    &parse_line_defaults_to_paragraph },
};

const mm::test::registrar reg{"mm.mdy unit", cases};

}
