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

const mm::test::case_ cases[] = {
    { "unterminated front matter keeps body",           &unterminated_front_matter_keeps_body, true },
};

const mm::test::registrar reg{"mm.mdy known defects", cases};

}
