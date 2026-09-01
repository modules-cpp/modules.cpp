// Black box tests for mm.shell's parse_script.
//
// parse_script recognizes only this project's own script style; see
// mm.shell's own module interface for what that means. These cases pin
// that scope, including what it deliberately gets "wrong" by real shell
// grammar, such as an indented assignment or "echo a=b" not being an
// assignment because "echo" is not followed by "=".

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

import mm.shell;
import mm.test;

namespace {

using mm::shell::LineKind;
using mm::shell::ScriptLine;

std::vector<ScriptLine> parse(std::string_view name, std::string_view text) {
    const mm::test::scoped_file file(name, text);
    return mm::shell::parse_script(file.path());
}

void classifies_a_comment_line() {
    const auto lines = parse("comment.sh", "# a comment\n");

    mm::test::expect(lines.size() == 1, "expected one line");
    mm::test::expect(lines[0].kind == LineKind::Comment, "expected a Comment line");
    mm::test::expect(lines[0].text == "# a comment", "expected the raw comment text");
}

void classifies_an_assignment_and_strips_double_quotes() {
    const auto lines = parse("assign-double.sh", "MM_BUILD=\"out\"\n");

    mm::test::expect(lines.size() == 1, "expected one line");
    mm::test::expect(lines[0].kind == LineKind::Assignment, "expected an Assignment line");
    mm::test::expect(lines[0].name == "MM_BUILD", "expected the variable name");
    mm::test::expect(lines[0].value == "out", "expected double quotes stripped from the value");
}

void classifies_an_assignment_with_single_quotes() {
    const auto lines = parse("assign-single.sh", "NAME='value'\n");

    mm::test::expect(lines[0].kind == LineKind::Assignment, "expected an Assignment line");
    mm::test::expect(lines[0].value == "value", "expected single quotes stripped from the value");
}

void classifies_an_unquoted_assignment() {
    const auto lines = parse("assign-bare.sh", "COUNT=3\n");

    mm::test::expect(lines[0].kind == LineKind::Assignment, "expected an Assignment line");
    mm::test::expect(lines[0].name == "COUNT", "expected the variable name");
    mm::test::expect(lines[0].value == "3", "expected the unquoted value unchanged");
}

void classifies_a_command_line() {
    const auto lines = parse("command.sh", "echo hello\n");

    mm::test::expect(lines[0].kind == LineKind::Command, "expected a Command line");
}

void a_word_followed_by_a_space_and_equals_is_not_an_assignment() {
    // "echo" is a word, but "=" does not immediately follow it, so this is
    // an ordinary command whose argument happens to contain "=".
    const auto lines = parse("echo-equals.sh", "echo a=b\n");

    mm::test::expect(lines[0].kind == LineKind::Command,
                     "expected a command whose argument contains '=' to stay a Command line");
}

void an_indented_assignment_is_still_an_assignment() {
    const auto lines = parse("indented.sh", "    NAME=value\n");

    mm::test::expect(lines[0].kind == LineKind::Assignment,
                     "expected leading whitespace to be skipped before classifying the line");
    mm::test::expect(lines[0].name == "NAME", "expected the variable name past the indentation");
}

void classifies_a_blank_line_as_empty() {
    const auto lines = parse("blank.sh", "\n");

    mm::test::expect(lines[0].kind == LineKind::Empty, "expected an Empty line");
}

void a_line_of_only_whitespace_is_empty() {
    const auto lines = parse("whitespace.sh", "   \t  \n");

    mm::test::expect(lines[0].kind == LineKind::Empty,
                     "expected a whitespace-only line to be Empty");
}

void keeps_every_line_in_order() {
    const auto lines = parse("mixed.sh", "# c\nA=1\necho hi\n\n");

    mm::test::expect(lines.size() == 4, "expected four lines");
    mm::test::expect(lines[0].kind == LineKind::Comment, "expected line 0 to be a Comment");
    mm::test::expect(lines[1].kind == LineKind::Assignment, "expected line 1 to be an Assignment");
    mm::test::expect(lines[2].kind == LineKind::Command, "expected line 2 to be a Command");
    mm::test::expect(lines[3].kind == LineKind::Empty, "expected line 3 to be Empty");
}

void a_missing_file_yields_no_lines() {
    const auto lines = mm::shell::parse_script("/nonexistent/path/does-not-exist.sh");

    mm::test::expect(lines.empty(), "expected no lines from a file that cannot be opened");
}

const mm::test::case_ cases[] = {
    { "classifies a comment line",                                  &classifies_a_comment_line },
    { "classifies an assignment and strips double quotes",          &classifies_an_assignment_and_strips_double_quotes },
    { "classifies an assignment with single quotes",                &classifies_an_assignment_with_single_quotes },
    { "classifies an unquoted assignment",                          &classifies_an_unquoted_assignment },
    { "classifies a command line",                                  &classifies_a_command_line },
    { "a word followed by a space and equals is not an assignment", &a_word_followed_by_a_space_and_equals_is_not_an_assignment },
    { "an indented assignment is still an assignment",              &an_indented_assignment_is_still_an_assignment },
    { "classifies a blank line as empty",                           &classifies_a_blank_line_as_empty },
    { "a line of only whitespace is empty",                         &a_line_of_only_whitespace_is_empty },
    { "keeps every line in order",                                  &keeps_every_line_in_order },
    { "a missing file yields no lines",                             &a_missing_file_yields_no_lines },
};

const mm::test::registrar reg{"mm.shell parse_script", cases};

}  // namespace
