// Regression tests for known mm.mdy parser defects: behaviour that
// contradicts docs/mdy.mdy, not behaviour a future format revision might
// add. Three cases previously here (a YAML-style block sequence, a leading
// blank line before the opening fence, and nested-list indentation) were
// removed: docs/mdy.mdy explicitly documents the opposite of what they
// expected (arrays are "not interpreted"; "the opening fence must be the
// first physical line"; nested lists are "not parsed specially"), so
// failing them pinned a prospective feature request against the current
// documented format, not a defect against it. Labeling a feature request as
// a defect makes docs/mdy.mdy and this file conflicting authorities on what
// the parser is supposed to do; if those three are ever wanted, they belong
// as a documented format change first, with tests added once docs/mdy.mdy
// says what the new behaviour should be.
//
// Every case in this file currently FAILS. Each one pins behaviour the
// parser is supposed to have; delete a case only when the corresponding
// defect is genuinely fixed, never to make the suite green.
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

MDYDocument parse_text(std::string_view text)
{
    const mm::test::scoped_file file{
        "/tmp/~modueles.cpp_mm_mdy_test_defect.mdy",
        text
    };
    return Parser::parse_file(file.path());
}

// Defect: a paragraph is still one physical line, so a wrapped run of plain
// lines parses as one Paragraph per line. docs/mdy.mdy now defines a paragraph
// as a maximal run of consecutive plain lines joined with a single space;
// the parser splits every line instead.
void paragraph_run_of_two_lines_is_one_block() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "First line of the paragraph.\n"
        "Second line of the same paragraph.\n");

    mm::test::expect(doc.body.size() == 1,
                     "expected the two plain lines to join into one paragraph");
    mm::test::expect(doc.body.size() == 1 &&
                     doc.body[0].content ==
                     "First line of the paragraph. Second line of the same paragraph.",
                     "expected the joined content with a single space between the lines");
}

void paragraph_run_of_three_lines_joins_with_single_spaces() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "one\n"
        "two\n"
        "three\n");

    mm::test::expect(doc.body.size() == 1, "expected the three plain lines to join into one paragraph");
    mm::test::expect(doc.body.size() == 1 && doc.body[0].content == "one two three",
                     "expected the run joined with single spaces, not newlines or double spaces");
}

void heading_ends_paragraph_run() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "one\n"
        "two\n"
        "# Title\n");

    mm::test::expect(doc.body.size() == 2,
                     "expected the run and the heading to form two blocks");
    mm::test::expect(doc.body.size() == 2 && doc.body[0].type == BlockType::Paragraph &&
                     doc.body[0].content == "one two",
                     "expected the joined run to end at the heading");
    mm::test::expect(doc.body.size() == 2 && doc.body[1].type == BlockType::Heading1 &&
                     doc.body[1].content == "Title",
                     "expected the heading to parse as its own block");
}

void list_ends_paragraph_run() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "one\n"
        "two\n"
        "- item\n");

    mm::test::expect(doc.body.size() == 2,
                     "expected the run and the list item to form two blocks");
    mm::test::expect(doc.body.size() == 2 && doc.body[0].type == BlockType::Paragraph &&
                     doc.body[0].content == "one two",
                     "expected the joined run to end at the list line");
    mm::test::expect(doc.body.size() == 2 && doc.body[1].type == BlockType::UnorderedList &&
                     doc.body[1].content == "item",
                     "expected the list line to parse as its own block");
}

void final_paragraph_run_at_end_of_file() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "one\n"
        "two\n");

    mm::test::expect(doc.body.size() == 1,
                     "expected the final run without a trailing empty line to parse");
    mm::test::expect(doc.body.size() == 1 && doc.body[0].content == "one two",
                     "expected the final run to join into one paragraph");
}

void run_ordering_with_headings_lists_and_blanks() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "---\n"
        "# Title\n"
        "one\n"
        "two\n"
        "- item\n"
        "\n"
        "three\n");

    mm::test::expect(doc.body.size() == 4, "expected four blocks from heading, run, list, run");
    mm::test::expect(doc.body.size() == 4 && doc.body[0].type == BlockType::Heading1 &&
                     doc.body[1].type == BlockType::Paragraph && doc.body[1].content == "one two" &&
                     doc.body[2].type == BlockType::UnorderedList &&
                     doc.body[3].type == BlockType::Paragraph && doc.body[3].content == "three",
                     "expected the run rule to hold across the whole body");
}

// Defect: front matter that is never closed swallows the whole file. Every
// remaining line is treated as metadata and the body comes back empty, which a
// caller cannot tell apart from a document that simply has no body.
void unterminated_front_matter_keeps_body() {
    const auto doc = parse_text(
        "---\n"
        "mm: 1.0\n"
        "# not metadata\n");

    mm::test::expect(doc.body.size() == 1,
                     "expected content after unterminated front matter to stay in the body");
}

const mm::test::case_ cases[] = {
    { "paragraph run of two lines is one block",        &paragraph_run_of_two_lines_is_one_block, true },
    { "paragraph run of three lines joins single spaced", &paragraph_run_of_three_lines_joins_with_single_spaces, true },
    { "heading ends paragraph run",                     &heading_ends_paragraph_run, true },
    { "list ends paragraph run",                        &list_ends_paragraph_run, true },
    { "final paragraph run at end of file",             &final_paragraph_run_at_end_of_file, true },
    { "run ordering with headings lists and blanks",    &run_ordering_with_headings_lists_and_blanks, true },
    { "unterminated front matter keeps body",           &unterminated_front_matter_keeps_body, true },
};

const mm::test::registrar reg{"mm.mdy known defects", cases};

}
