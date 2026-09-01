// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

export module mm.test;

export namespace mm::test {

// Scratch files and manifest trees under the system temp directory, for
// tests that need real files on disk rather than a string in memory. Both
// remove what they created when the case leaves scope, so a failing test
// cannot leave a half written tree behind for the next run to trip over.
//
// These live here, in the test framework, because five test files carried
// byte identical copies of scoped_file and three carried scoped_tree: one
// copy is one place to fix a bug in them.

// A file holding the given text, removed again at end of scope. A relative
// name is placed under the temp directory; an absolute one is used as is.
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
    scoped_file(scoped_file&&) = delete;
    scoped_file& operator=(scoped_file&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// A manifest tree, removed again at end of scope. name must be unique
// across every suite, since all trees share one prefix under the temp
// directory.
class scoped_tree {
public:
    explicit scoped_tree(std::string_view name)
        : root_(std::filesystem::temp_directory_path() / ("mm_test_" + std::string(name))) {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_, ec);
    }

    ~scoped_tree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    scoped_tree(const scoped_tree&) = delete;
    scoped_tree& operator=(const scoped_tree&) = delete;
    scoped_tree(scoped_tree&&) = delete;
    scoped_tree& operator=(scoped_tree&&) = delete;

    // Writes <relative>/mm.mdy, creating the directory. The current mm:
    // version is supplied, so cases that do not care about it say nothing.
    void manifest(std::string_view relative, std::string_view front_matter) const {
        write(relative, std::string("mm: 1.0\n") + std::string(front_matter));
    }

    // The same without any mm: line, for cases controlling that key
    // themselves: missing, unsupported, or repeated.
    void manifest_raw(std::string_view relative, std::string_view front_matter) const {
        write(relative, std::string(front_matter));
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    void write(std::string_view relative, const std::string& front_matter) const {
        const auto dir = relative.empty() ? root_ : root_ / relative;

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::ofstream out(dir / "mm.mdy");
        out << "---\n" << front_matter << "---\n";
    }

    std::filesystem::path root_;
};

struct failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(
    bool condition,
    std::string_view message,
    std::source_location where = std::source_location::current()
);

using fn = void (*)();

struct case_ {
    std::string_view name;
    fn run;

    // A case pinning behaviour the code does not have yet. Failing is expected
    // and does not fail the run; passing does, because the marker is then stale
    // and the case belongs with the ordinary tests.
    bool expected_failure = false;
};

void add_suite(std::string_view name, const case_* cases, std::size_t count);
int run_all();

struct registrar {
    registrar(std::string_view name, const case_* cases, std::size_t count) {
        add_suite(name, cases, count);
    }

    template <std::size_t N>
    registrar(std::string_view name, const case_ (&cases)[N]) {
        add_suite(name, cases, N);
    }
};

}

