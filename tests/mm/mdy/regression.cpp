// Regression tests for mm.mdy.
//
// These pin the parser contract that the manifest driven tools depend on. They
// exist because of two review findings:
//
//   1. a fresh checkout could not bootstrap, because modules/mm/build was
//      ignored by git;
//   2. cyclic folder: entries made the build tool recurse forever.
//
// Neither defect was in mm.mdy, and the fixes were in .gitignore and in
// mm.build's walker. What both findings did expose is how much the tools trust
// this parser: the walker's cycle detection only works if folder: values arrive
// verbatim, and its dependency ordering only works if repeated keys keep their
// declared order. A silent change here would reintroduce a hang or a misordered
// compile without touching mm.build at all.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.mdy;
import mm.test;

namespace {

using mm::mdy::MDYDocument;
using mm::mdy::Parser;

MDYDocument parse_text(std::string_view text)
{
    const mm::test::scoped_file file{
        "/tmp/~modueles.cpp_mm_mdy_test_regressin.mdy",
        text
    };
    return Parser::parse_file(file.path());
}

const std::vector<std::string>* values(const MDYDocument& doc, std::string_view key) {
    const auto it = doc.metadata.find(key);
    return it == doc.metadata.end() ? nullptr : &it->second;
}

std::string first(const MDYDocument& doc, std::string_view key) {
    const auto* found = values(doc, key);
    return found == nullptr || found->empty() ? std::string{} : found->front();
}

// --- path shaped values -------------------------------------------------

// A self referencing folder: entry is what made the walker hang. The parser has
// to hand back "." unchanged for the cycle check to have anything to compare.
void keeps_dot_as_a_folder_value() {
    const auto doc = parse_text(
        "---\n"
        "kind: project\n"
        "folder: .\n"
        "---\n");

    mm::test::expect(first(doc, "folder") == ".",
                     "expected a lone dot to survive as a folder value");
}

// The two manifest cycle was built from ../a and ../b. If the parser normalised
// or stripped these the walker could not resolve them and could not detect the
// loop.
void keeps_relative_paths_verbatim() {
    const auto doc = parse_text(
        "---\n"
        "kind: dir\n"
        "folder: ../b\n"
        "folder: ../..\n"
        "folder: modules/mm/mdy\n"
        "---\n");

    const auto* found = values(doc, "folder");
    mm::test::expect(found != nullptr && found->size() == 3, "expected three folder values");
    mm::test::expect((*found)[0] == "../b", "expected ../b to survive unchanged");
    mm::test::expect((*found)[1] == "../..", "expected ../.. to survive unchanged");
    mm::test::expect((*found)[2] == "modules/mm/mdy", "expected a nested path to survive unchanged");
}

// use: mm.build resolves to a module name, not a path. A dotted value must not
// be split or truncated at the dot.
void keeps_dotted_module_names_whole() {
    const auto doc = parse_text(
        "---\n"
        "kind: module\n"
        "module: mm.mdy\n"
        "use: mm.build\n"
        "---\n");

    mm::test::expect(first(doc, "module") == "mm.mdy", "expected a dotted module name to stay whole");
    mm::test::expect(first(doc, "use") == "mm.build", "expected a dotted use value to stay whole");
}

void keeps_file_extensions() {
    const auto doc = parse_text(
        "---\n"
        "file: src/mdy.cpp\n"
        "file: mdy_types.cppm\n"
        "---\n");

    const auto* found = values(doc, "file");
    mm::test::expect(found != nullptr && found->size() == 2, "expected two file values");
    mm::test::expect((*found)[0] == "src/mdy.cpp", "expected a path with an extension to survive");
    mm::test::expect((*found)[1] == "mdy_types.cppm", "expected a .cppm extension to survive");
}

// --- ordering -----------------------------------------------------------

// Declaration order is dependency order across the whole toolchain: partitions
// must compile before the interface that imports them. The parser is the only
// thing preserving that order.
void repeated_keys_keep_declared_order() {
    const auto doc = parse_text(
        "---\n"
        "file: mdy_types.cppm\n"
        "file: mdy_impl.cppm\n"
        "file: mdy.cppm\n"
        "file: src/mdy.cpp\n"
        "---\n");

    const auto* found = values(doc, "file");
    mm::test::expect(found != nullptr && found->size() == 4, "expected four file values");
    mm::test::expect((*found)[0] == "mdy_types.cppm", "expected the types partition first");
    mm::test::expect((*found)[1] == "mdy_impl.cppm", "expected the impl partition second");
    mm::test::expect((*found)[2] == "mdy.cppm", "expected the primary interface third");
    mm::test::expect((*found)[3] == "src/mdy.cpp", "expected the implementation unit last");
}

// Interleaving must not regroup values: the tools read one key's list in order
// without assuming the keys themselves are contiguous in the file.
void interleaved_keys_keep_per_key_order() {
    const auto doc = parse_text(
        "---\n"
        "file: a.cppm\n"
        "use: mm.one\n"
        "file: b.cppm\n"
        "use: mm.two\n"
        "---\n");

    const auto* files = values(doc, "file");
    const auto* uses = values(doc, "use");

    mm::test::expect(files != nullptr && files->size() == 2, "expected two file values");
    mm::test::expect((*files)[0] == "a.cppm" && (*files)[1] == "b.cppm",
                     "expected file order to survive interleaving");
    mm::test::expect(uses != nullptr && uses->size() == 2, "expected two use values");
    mm::test::expect((*uses)[0] == "mm.one" && (*uses)[1] == "mm.two",
                     "expected use order to survive interleaving");
}

// Every tool reads scalars with a first() helper, so a duplicated scalar key has
// to resolve to the first occurrence rather than the last.
void first_value_wins_for_scalar_keys() {
    const auto doc = parse_text(
        "---\n"
        "kind: module\n"
        "kind: app\n"
        "---\n");

    mm::test::expect(first(doc, "kind") == "module", "expected the first value of a scalar key");
}

// --- shapes that must not become something else -------------------------

// The tools branch on kind. A missing key has to be absent rather than present
// and empty, so that a manifest without a kind reports a manifest error instead
// of matching a branch.
void missing_key_is_absent() {
    const auto doc = parse_text(
        "---\n"
        "name: only\n"
        "---\n");

    mm::test::expect(doc.metadata.find("kind") == doc.metadata.end(),
                     "expected an undeclared key to be absent from the metadata");
    mm::test::expect(values(doc, "name") != nullptr, "expected the declared key to be present");
}

// A key written with no value keeps the key. The walker turns an empty folder
// value into a path that resolves back to its own directory, which its cycle
// check then catches; dropping the key would hide the mistake instead.
void empty_value_keeps_the_key() {
    const auto doc = parse_text(
        "---\n"
        "folder:\n"
        "---\n");

    const auto* found = values(doc, "folder");
    mm::test::expect(found != nullptr && found->size() == 1, "expected the key to survive");
    mm::test::expect(found->front().empty(), "expected an empty value rather than a dropped key");
}

// Manifests are hand written, so padding is expected and must not reach a path.
void pads_are_trimmed_from_paths() {
    const auto doc = parse_text(
        "---\n"
        "file:   src/mdy.cpp   \n"
        "\tfolder:\t../b\t\n"
        "---\n");

    mm::test::expect(first(doc, "file") == "src/mdy.cpp",
                     "expected surrounding spaces to be trimmed from a path");
    mm::test::expect(first(doc, "folder") == "../b",
                     "expected tabs around a key and value to be trimmed");
}

// Front matter is a fixed set of scalar keys, not a nested document: an indented
// line is still a plain key, not a child of the line above it.
void indented_entries_are_not_nested() {
    const auto doc = parse_text(
        "---\n"
        "kind: dir\n"
        "  folder: a\n"
        "---\n");

    mm::test::expect(first(doc, "folder") == "a", "expected an indented entry to parse as a key");
    mm::test::expect(doc.metadata.size() == 2, "expected exactly two keys");
}

const mm::test::case_ cases[] = {
    { "keeps dot as a folder value",           &keeps_dot_as_a_folder_value },
    { "keeps relative paths verbatim",         &keeps_relative_paths_verbatim },
    { "keeps dotted module names whole",       &keeps_dotted_module_names_whole },
    { "keeps file extensions",                 &keeps_file_extensions },
    { "repeated keys keep declared order",     &repeated_keys_keep_declared_order },
    { "interleaved keys keep per key order",   &interleaved_keys_keep_per_key_order },
    { "first value wins for scalar keys",      &first_value_wins_for_scalar_keys },
    { "missing key is absent",                 &missing_key_is_absent },
    { "empty value keeps the key",             &empty_value_keeps_the_key },
    { "pads are trimmed from paths",           &pads_are_trimmed_from_paths },
    { "indented entries are not nested",       &indented_entries_are_not_nested },
};

const mm::test::registrar reg{"mm.mdy regression", cases};

}
