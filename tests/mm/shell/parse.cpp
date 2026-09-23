// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::EmbeddedScript;
using mm::shell::ParseStatus;
using mm::shell::ScriptStorage;
using mm::shell::SourceView;
using mm::shell::StorageClass;
using mm::test::expect;

struct Storage {
    std::array<mm::shell::ScriptToken, 128> tokens{};
    std::array<mm::shell::WordFragment, 128> fragments{};
    std::array<mm::shell::SyntaxNode, 128> nodes{};
    std::array<mm::shell::SyntaxLink, 128> links{};
    std::array<mm::shell::ParserFrame, 64> context{};

    [[nodiscard]] ScriptStorage view() {
        return {tokens, fragments, nodes, links, context};
    }
};

void all_embedded_productions() {
    constexpr std::array<std::string_view, 13> scripts{
        "",
        "a=1 echo $a && echo yes || false\n",
        "! test 1 -eq 2\n",
        "if true; then echo yes; elif false; then echo no; "
        "else echo other; fi\n",
        "for x in a b; do echo $x; done\n",
        "for x; do echo $x; done\n",
        "while false; do yield; done\n",
        "case $x in a|b) echo yes;; *) echo no;; esac\n",
        "case $x in [a|b]) echo yes;; esac\n",
        "f() { echo ok; }\n",
        "{ echo one; echo two; }\n",
        "echo 'literal *' \"$@\" \\* ${10} $((1+2)) $(echo x)\n",
        "# comment\r\necho [ x = x ]\r\n"};
    for (const auto source : scripts) {
        const auto measured = mm::shell::measure_embedded(
            SourceView{source});
        expect(measured.status == ParseStatus::Complete,
               "embedded production measures completely");
        Storage storage;
        EmbeddedScript script;
        const auto parsed = mm::shell::parse_embedded(
            SourceView{source}, storage.view(), script);
        expect(parsed.status == ParseStatus::Complete,
               "embedded production parses completely");
        expect(parsed.required.tokens == measured.required.tokens &&
                   parsed.required.fragments == measured.required.fragments &&
                   parsed.required.nodes == measured.required.nodes &&
                   parsed.required.links == measured.required.links &&
                   parsed.required.context == measured.required.context,
               "count and build follow the same grammar");
        expect(script.tokens.size() == measured.required.tokens &&
                   script.nodes.size() == measured.required.nodes,
               "script spans expose exact measured counts");
        expect(script.nodes[script.root].kind ==
                   mm::shell::SyntaxKind::Program,
               "syntax root is a program");
        if (source.starts_with("for x")) {
            bool found_for = false;
            for (const auto& node : script.nodes) {
                if (node.kind != mm::shell::SyntaxKind::For) continue;
                found_for = true;
                expect(node.has_in_clause == source.starts_with(
                           "for x in"),
                       "for AST preserves explicit in clause");
            }
            expect(found_for, "for AST node is present");
        }
        if (source.starts_with("case $x in [a|b]")) {
            bool found_item = false;
            for (const auto& node : script.nodes) {
                if (node.kind != mm::shell::SyntaxKind::CaseItem) continue;
                found_item = true;
                expect(node.link_count == 2,
                       "bar inside bracket pattern is not an alternative");
            }
            expect(found_item, "bracket case item is present");
        }
    }
}

void incomplete_and_malformed() {
    constexpr std::array<std::string_view, 9> incomplete{
        "echo '", "echo $(echo", "echo &&", "if true; then",
        "for x in a; do", "while true; do", "case x in a)",
        "f() { echo x", "echo $(if true; then echo x)"};
    for (const auto source : incomplete) {
        const auto result = mm::shell::measure_embedded(
            SourceView{source});
        expect(result.status == ParseStatus::Incomplete,
               "unterminated production is incomplete");
    }
    constexpr std::array<std::string_view, 11> malformed{
        "echo x | cat", "echo > file", "echo &", "echo `date`",
        "then echo x", "echo *.c", "echo x; ; echo y",
        "if; then true; fi", "for 2bad; do true; done",
        "case x in a|) true;; esac", "echo $(echo x | cat)"};
    for (const auto source : malformed) {
        const auto result = mm::shell::measure_embedded(
            SourceView{source});
        expect(result.status == ParseStatus::Malformed,
               "invalid production is malformed");
    }
    Storage storage;
    EmbeddedScript unchanged;
    unchanged.root = 91;
    storage.tokens[0].source.offset = 91;
    const auto bad = mm::shell::parse_embedded(
        SourceView{"echo > file"}, storage.view(), unchanged);
    expect(bad.status == ParseStatus::Malformed && bad.issue.offset == 5,
           "unsupported redirection reports its first byte");
    expect(unchanged.root == 91 &&
               storage.tokens[0].source.offset == 91,
           "malformed preflight leaves output untouched");
}

void exact_storage_preflight() {
    constexpr std::string_view source = "echo a'b' && echo ${10}\n";
    const auto measured = mm::shell::measure_embedded(SourceView{source});
    expect(measured.status == ParseStatus::Complete,
           "boundary source measures");
    const auto required = measured.required;
    expect(required.tokens == 6 && required.fragments == 5 &&
               required.nodes == 9 && required.links == 8 &&
               required.context == 4,
           "boundary source has exact resource counts");
    expect(required.tokens > 0 && required.fragments > 0 &&
               required.nodes > 0 && required.links > 0 &&
               required.context > 0,
           "all storage classes are exercised");
    Storage storage;
    EmbeddedScript output;
    output.root = 91;
    storage.tokens[0].source.offset = 91;
    storage.fragments[0].source.offset = 91;
    storage.nodes[0].source.offset = 91;
    storage.links[0].child = 91;
    storage.context[0].kind = mm::shell::SyntaxKind::Case;
    const auto full = storage.view();
    constexpr std::array classes{
        StorageClass::Tokens, StorageClass::WordFragments,
        StorageClass::SyntaxNodes, StorageClass::SyntaxLinks,
        StorageClass::ParserContext};
    for (const auto storage_class : classes) {
        auto short_view = full;
        std::size_t expected = 0;
        switch (storage_class) {
            case StorageClass::Tokens:
                short_view.tokens = full.tokens.first(required.tokens - 1);
                expected = required.tokens;
                break;
            case StorageClass::WordFragments:
                short_view.fragments = full.fragments.first(
                    required.fragments - 1);
                expected = required.fragments;
                break;
            case StorageClass::SyntaxNodes:
                short_view.nodes = full.nodes.first(required.nodes - 1);
                expected = required.nodes;
                break;
            case StorageClass::SyntaxLinks:
                short_view.links = full.links.first(required.links - 1);
                expected = required.links;
                break;
            case StorageClass::ParserContext:
                short_view.context = full.context.first(
                    required.context - 1);
                expected = required.context;
                break;
            default: break;
        }
        const auto result = mm::shell::parse_embedded(
            SourceView{source}, short_view, output);
        expect(result.status == ParseStatus::Overflow &&
                   result.overflow.storage_class == storage_class &&
                   result.overflow.required == expected,
               "one-short storage reports exact class and count");
        expect(output.root == 91 &&
                   storage.tokens[0].source.offset == 91 &&
                   storage.fragments[0].source.offset == 91 &&
                   storage.nodes[0].source.offset == 91 &&
                   storage.links[0].child == 91 &&
                   storage.context[0].kind == mm::shell::SyntaxKind::Case,
               "preflight failure leaves script and storage unchanged");
    }
    const auto result = mm::shell::parse_embedded(
        SourceView{source}, full, output);
    expect(result.status == ParseStatus::Complete,
           "exact measured storage parses");
}

void independent_parsers() {
    Storage left_storage;
    Storage right_storage;
    EmbeddedScript left;
    EmbeddedScript right;
    constexpr std::string_view left_source = "echo left";
    constexpr std::string_view right_source = "echo right";
    const auto a = mm::shell::parse_embedded(SourceView{left_source},
                                             left_storage.view(), left);
    const auto b = mm::shell::parse_embedded(SourceView{right_source},
                                             right_storage.view(), right);
    expect(a.status == ParseStatus::Complete &&
               b.status == ParseStatus::Complete,
           "independent parsers complete");
    expect(left.source.text() == left_source &&
               right.source.text() == right_source,
           "each script keeps its own immutable source view");
    expect(left.tokens.data() != right.tokens.data(),
           "each script uses independent caller storage");
}

const mm::test::case_ cases[]{
    {"all embedded productions", &all_embedded_productions},
    {"incomplete and malformed", &incomplete_and_malformed},
    {"exact storage preflight", &exact_storage_preflight},
    {"independent parsers", &independent_parsers},
};

const mm::test::registrar reg{"mm.shell parser", cases};

}  // namespace
