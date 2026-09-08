// Black box tests for mm.build's shell quoting.
//
// These exist because of a review finding: paths were wrapped in double quotes
// before being handed to std::system, and $(), backticks and $NAME all expand
// inside double quotes. A manifest naming a file "$(touch EXECUTED)x.cppm"
// therefore ran touch. Single quoting closes that, and these cases pin it.
//
// The rule being tested is simple: between single quotes /bin/sh performs no
// expansion at all, and the only character that cannot appear there is the
// single quote itself.

#include <filesystem>
#include <string>
#include <string_view>

import mm.build;
import mm.test;

namespace {

using mm::build::shell_quote;

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

void wraps_a_plain_path_in_single_quotes() {
    mm::test::expect(shell_quote(std::filesystem::path("m/x.cppm")) == "'m/x.cppm'",
                     "expected a plain path to be wrapped in single quotes");
}

void quotes_an_empty_path() {
    mm::test::expect(shell_quote(std::filesystem::path("")) == "''",
                     "expected an empty path to become an empty quoted argument");
}

// The payload from the finding. Inside single quotes the shell sees the text,
// not a command.
void neutralises_command_substitution() {
    const auto quoted = shell_quote(std::filesystem::path("$(touch EXECUTED)x.cppm"));

    mm::test::expect(quoted == "'$(touch EXECUTED)x.cppm'",
                     "expected a command substitution to be quoted verbatim");
    mm::test::expect(!contains(quoted, "\""), "expected no double quotes in the result");
}

void neutralises_backticks() {
    mm::test::expect(shell_quote(std::filesystem::path("a`id`b.cppm")) == "'a`id`b.cppm'",
                     "expected backticks to be quoted verbatim");
}

void neutralises_variable_expansion() {
    mm::test::expect(shell_quote(std::filesystem::path("v$HOME.cppm")) == "'v$HOME.cppm'",
                     "expected a variable reference to be quoted verbatim");
    mm::test::expect(shell_quote(std::filesystem::path("v$1.cppm")) == "'v$1.cppm'",
                     "expected a positional parameter to be quoted verbatim");
}

// A double quote inside the path used to terminate the old quoting and let the
// rest of the name become further arguments.
void neutralises_a_double_quote() {
    mm::test::expect(shell_quote(std::filesystem::path("quote\".cppm")) == "'quote\".cppm'",
                     "expected a double quote to be harmless inside single quotes");
}

void neutralises_semicolons_and_pipes() {
    mm::test::expect(shell_quote(std::filesystem::path("a;rm -rf x|b.cppm")) ==
                         "'a;rm -rf x|b.cppm'",
                     "expected command separators to be quoted verbatim");
}

void keeps_backslashes_literal() {
    mm::test::expect(shell_quote(std::filesystem::path("back\\slash.cppm")) ==
                         "'back\\slash.cppm'",
                     "expected a backslash to stay literal inside single quotes");
}

void keeps_spaces_in_one_argument() {
    mm::test::expect(shell_quote(std::filesystem::path("plain space.cppm")) ==
                         "'plain space.cppm'",
                     "expected a path with a space to remain one argument");
}

// The one case single quoting cannot express directly: close the run, emit an
// escaped quote, reopen. 'it'\''s.cppm' is what the shell reassembles into
// it's.cppm.
void escapes_an_embedded_single_quote() {
    mm::test::expect(shell_quote(std::filesystem::path("it's.cppm")) == "'it'\\''s.cppm'",
                     "expected an embedded single quote to be closed, escaped and reopened");
}

void escapes_a_leading_single_quote() {
    mm::test::expect(shell_quote(std::filesystem::path("'x.cppm")) == "''\\''x.cppm'",
                     "expected a leading single quote to be escaped");
}

void escapes_repeated_single_quotes() {
    mm::test::expect(shell_quote(std::filesystem::path("a''b")) == "'a'\\'''\\''b'",
                     "expected each embedded single quote to be escaped");
}

const mm::test::case_ cases[] = {
    { "wraps a plain path in single quotes",  &wraps_a_plain_path_in_single_quotes },
    { "quotes an empty path",                 &quotes_an_empty_path },
    { "neutralises command substitution",     &neutralises_command_substitution },
    { "neutralises backticks",                &neutralises_backticks },
    { "neutralises variable expansion",       &neutralises_variable_expansion },
    { "neutralises a double quote",           &neutralises_a_double_quote },
    { "neutralises semicolons and pipes",     &neutralises_semicolons_and_pipes },
    { "keeps backslashes literal",            &keeps_backslashes_literal },
    { "keeps spaces in one argument",         &keeps_spaces_in_one_argument },
    { "escapes an embedded single quote",     &escapes_an_embedded_single_quote },
    { "escapes a leading single quote",       &escapes_a_leading_single_quote },
    { "escapes repeated single quotes",       &escapes_repeated_single_quotes },
};

const mm::test::registrar reg{"mm.build quoting", cases};

}
