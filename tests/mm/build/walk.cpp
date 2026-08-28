// Black box tests for mm.build's manifest walk.
//
// These build throwaway manifest trees under the system temp directory and hand
// them to load_tree. The hostile cases come from a review finding: a folder:
// entry naming ".", a two manifest loop and a symlink loop all made the walk
// recurse until it was killed. Each must now terminate with ok == false.
//
// Negative cases deliberately drive load_tree into its error paths, so this
// suite prints "build: ..." diagnostics to stderr while passing. That output is
// the behaviour under test, not a failure.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
import mm.test;

namespace {

// A manifest tree that deletes itself when the test case leaves scope.
class scoped_tree {
public:
    explicit scoped_tree(std::string_view name)
        : root_(std::filesystem::temp_directory_path() / ("mm_build_test_" + std::string(name))) {
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

    // Writes <relative>/mm.mdy, creating the directory.
    void manifest(std::string_view relative, std::string_view front_matter) const {
        const auto dir = relative.empty() ? root_ : root_ / relative;

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::ofstream out(dir / "mm.mdy");
        out << "---\nmm: 0.1\n" << front_matter << "---\n";
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

bool has_target(const mm::build::Tree& tree, std::string_view name) {
    for (const auto& target : tree.targets)
        if (target.name == name) return true;
    return false;
}

int count_targets(const mm::build::Tree& tree, std::string_view name) {
    int found = 0;
    for (const auto& target : tree.targets)
        if (target.name == name) ++found;
    return found;
}

// --- the shape a healthy tree produces ----------------------------------

void walks_a_nested_tree() {
    const scoped_tree tree{"nested"};
    tree.manifest("", "kind: project\nname: p\nfolder: modules\n");
    tree.manifest("modules", "kind: dir\nname: modules\nfolder: one\nfolder: two\n");
    tree.manifest("modules/one", "kind: module\nname: one\nmodule: mm.one\nfile: one.cppm\n");
    tree.manifest("modules/two", "kind: app\nname: two\nfile: two.cpp\nuse: mm.one\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected a well formed tree to load");
    mm::test::expect(loaded.targets.size() == 2, "expected two targets");
    mm::test::expect(has_target(loaded, "one"), "expected the module target");
    mm::test::expect(has_target(loaded, "two"), "expected the app target");
}

void separates_tests_and_docs_from_targets() {
    const scoped_tree tree{"kinds"};
    tree.manifest("", "kind: project\nname: p\nfolder: m\nfolder: t\nfolder: d\n");
    tree.manifest("m", "kind: module\nname: m\nmodule: mm.m\nfile: m.cppm\n");
    tree.manifest("t", "kind: test\nname: t\nunit: t.cpp\n");
    tree.manifest("d", "kind: doc\nname: d\nfile: d.mdy\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected the tree to load");
    mm::test::expect(loaded.targets.size() == 1, "expected only the module among build targets");
    mm::test::expect(loaded.tests.size() == 1, "expected the test target to be collected separately");
    mm::test::expect(loaded.docs.size() == 1, "expected the doc target to be collected separately");
}

// --- cycles must terminate ----------------------------------------------

void rejects_self_referencing_folder() {
    const scoped_tree tree{"selfref"};
    tree.manifest("", "kind: project\nname: p\nfolder: .\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected folder: . to be rejected rather than followed forever");
}

void rejects_two_manifest_cycle() {
    const scoped_tree tree{"cycle2"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: dir\nname: a\nfolder: ../b\n");
    tree.manifest("b", "kind: dir\nname: b\nfolder: ../a\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a folder: loop between two manifests to be rejected");
}

void rejects_symlink_cycle() {
    const scoped_tree tree{"symlink"};
    tree.manifest("", "kind: project\nname: p\nfolder: real\n");
    tree.manifest("real", "kind: dir\nname: r\nfolder: link\n");

    std::error_code ec;
    std::filesystem::create_directory_symlink("..", tree.root() / "real" / "link", ec);
    if (ec) return;  // filesystem without symlinks; nothing to assert

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a symlink that loops back to be rejected");
}

void rejects_traversal_above_the_root() {
    const scoped_tree tree{"escape"};
    tree.manifest("", "kind: project\nname: p\nfolder: sub\n");
    tree.manifest("sub", "kind: dir\nname: s\nfolder: ../..\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a folder: entry climbing above the root to be rejected");
}

// A diamond is not a cycle: two directories may legitimately name one shared
// folder, and it must be visited once rather than duplicated or rejected.
void accepts_a_diamond_once() {
    const scoped_tree tree{"diamond"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\nfolder: b\n");
    tree.manifest("a", "kind: dir\nname: a\nfolder: ../shared\n");
    tree.manifest("b", "kind: dir\nname: b\nfolder: ../shared\n");
    tree.manifest("shared", "kind: module\nname: s\nmodule: mm.s\nfile: s.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected a shared folder to be accepted");
    mm::test::expect(count_targets(loaded, "s") == 1,
                     "expected a shared folder to produce exactly one target");
}

// --- malformed manifests ------------------------------------------------

void rejects_missing_manifest() {
    const scoped_tree tree{"missing"};
    tree.manifest("", "kind: project\nname: p\nfolder: gone\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a folder: entry with no manifest to be rejected");
}

void rejects_unknown_kind() {
    const scoped_tree tree{"kind"};
    tree.manifest("", "kind: project\nname: p\nfolder: x\n");
    tree.manifest("x", "kind: library\nname: x\nfile: x.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected an unknown kind to be rejected");
}

void rejects_module_without_module_name() {
    const scoped_tree tree{"nomodule"};
    tree.manifest("", "kind: project\nname: p\nfolder: m\n");
    tree.manifest("m", "kind: module\nname: m\nfile: m.cppm\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected a module without a module: name to be rejected");
}

void rejects_target_without_sources() {
    const scoped_tree tree{"nofiles"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: app\nname: a\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(!loaded.ok, "expected an app with no file: entries to be rejected");
}

// A doc target is prose, so an empty file: list is allowed where it would be an
// error for anything that gets compiled.
void accepts_doc_without_files() {
    const scoped_tree tree{"emptydoc"};
    tree.manifest("", "kind: project\nname: p\nfolder: d\n");
    tree.manifest("d", "kind: doc\nname: d\n");

    const auto loaded = mm::build::load_tree(tree.root());

    mm::test::expect(loaded.ok, "expected a doc target with no files to be accepted");
    mm::test::expect(loaded.docs.size() == 1, "expected the doc target to be collected");
}

const mm::test::case_ cases[] = {
    { "walks a nested tree",                  &walks_a_nested_tree },
    { "separates tests and docs",             &separates_tests_and_docs_from_targets },
    { "rejects self referencing folder",      &rejects_self_referencing_folder },
    { "rejects two manifest cycle",           &rejects_two_manifest_cycle },
    { "rejects symlink cycle",                &rejects_symlink_cycle },
    { "rejects traversal above the root",     &rejects_traversal_above_the_root },
    { "accepts a diamond once",               &accepts_a_diamond_once },
    { "rejects missing manifest",             &rejects_missing_manifest },
    { "rejects unknown kind",                 &rejects_unknown_kind },
    { "rejects module without module name",   &rejects_module_without_module_name },
    { "rejects target without sources",       &rejects_target_without_sources },
    { "accepts doc without files",            &accepts_doc_without_files },
};

const mm::test::registrar reg{"mm.build walk", cases};

}
