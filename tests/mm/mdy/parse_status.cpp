// Black box tests for MDYDocument::status. Parser::parse_file previously
// returned a default constructed, empty MDYDocument for a missing file, an
// unreadable one, an empty one, and various malformed inputs alike, with no
// way for a caller to tell them apart. status distinguishes the two real
// I/O failures (NotFound, Unreadable) from Ok, which now also covers a
// legitimately empty document - an empty file is not an error.

#include <filesystem>
#include <fstream>
#include <string_view>

import mm.mdy;
import mm.test;

namespace {

using mm::mdy::ParseStatus;
using mm::mdy::Parser;

// A file that deletes itself when the test case leaves scope.
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

void parse_file_of_a_valid_document_is_ok() {
    const scoped_file file{"mm_mdy_test_status_ok.mdy", "---\nmm: 0.1\n---\n# Heading\n"};
    const auto doc = Parser::parse_file(file.path());
    mm::test::expect(doc.status == ParseStatus::Ok, "expected a valid document to parse as Ok");
}

void parse_file_of_an_empty_file_is_ok() {
    const scoped_file file{"mm_mdy_test_status_empty.mdy", ""};
    const auto doc = Parser::parse_file(file.path());
    mm::test::expect(doc.status == ParseStatus::Ok,
                     "expected an empty file to be a legitimately empty document, not an error");
    mm::test::expect(doc.metadata.empty() && doc.body.empty(),
                     "expected an empty file to produce an empty document");
}

void parse_file_of_a_missing_path_is_not_found() {
    const auto path = std::filesystem::temp_directory_path() / "mm_mdy_test_status_missing.mdy";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    const auto doc = Parser::parse_file(path);
    mm::test::expect(doc.status == ParseStatus::NotFound,
                     "expected a missing path to report NotFound rather than an empty Ok document");
}

// A directory exists at the given path but cannot be opened as an ifstream,
// giving Unreadable without needing filesystem permission tricks that would
// not be portable across test environments.
void parse_file_of_a_directory_is_unreadable() {
    const auto path = std::filesystem::temp_directory_path() / "mm_mdy_test_status_dir.mdy";
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);

    const auto doc = Parser::parse_file(path);
    std::filesystem::remove_all(path, ec);

    mm::test::expect(doc.status == ParseStatus::Unreadable,
                     "expected a path that exists but cannot be opened to report Unreadable");
}

const mm::test::case_ cases[] = {
    { "parse_file of a valid document is Ok",         &parse_file_of_a_valid_document_is_ok },
    { "parse_file of an empty file is Ok",            &parse_file_of_an_empty_file_is_ok },
    { "parse_file of a missing path is NotFound",     &parse_file_of_a_missing_path_is_not_found },
    { "parse_file of a directory is Unreadable",      &parse_file_of_a_directory_is_unreadable },
};

const mm::test::registrar reg{"mm.mdy parse status", cases};

}  // namespace
