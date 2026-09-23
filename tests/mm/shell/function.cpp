// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::FunctionLibrary;
using mm::shell::FunctionStorage;
using mm::shell::SourceView;
using mm::shell::Status;
using mm::test::expect;

struct Scratch {
    mm::shell::FunctionSlot functions[2]{};
    char text[128]{};
    mm::shell::ScriptToken tokens[32]{};
    mm::shell::WordFragment fragments[32]{};
    mm::shell::SyntaxNode nodes[32]{};
    mm::shell::SyntaxLink links[32]{};
    mm::shell::ParserFrame context[16]{};

    [[nodiscard]] FunctionStorage storage() {
        return {functions, text, tokens, fragments, nodes,
                links, context};
    }
};

void deep_copy_and_replacement() {
    Scratch scratch;
    FunctionLibrary library{scratch.storage()};
    char source[] = "echo old";
    const auto first = library.define("demo", SourceView{source});
    expect(first.ok() && library.size() == 1 &&
               library.find("demo") != nullptr,
           "definition publishes a named function");
    source[5] = 'n';
    const auto& copied = library.find("demo")->body;
    expect(copied.source.text() == "echo old" &&
               !copied.fragments.empty() &&
               copied.source.slice(copied.fragments[0].source) == "echo" &&
               !copied.nodes.empty(),
           "function source is copied out of the caller buffer");
    const auto second = library.define("demo", SourceView{"echo new"});
    expect(second.ok() && library.size() == 1 &&
               library.find("demo")->body.source.text() == "echo new",
           "replacement publishes one new visible handle");
    expect(library.define("other", SourceView{"true"}).ok() &&
               library.size() == 2,
           "independent function uses the second slot");
    const auto full = library.define("third", SourceView{"true"});
    expect(full.status == Status::Overflow &&
               full.overflow.storage_class ==
                   mm::shell::StorageClass::Functions &&
               library.find("third") == nullptr,
           "slot overflow leaves the table unchanged");
    library.reset();
    expect(library.size() == 0 && library.find("demo") == nullptr &&
               library.define("third", SourceView{"true"}).ok(),
           "reset reclaims monotonic storage");
}

void replacement_preflight_and_names() {
    Scratch scratch;
    auto storage = scratch.storage();
    storage.text = std::span{scratch.text}.first(24);
    FunctionLibrary library{storage};
    expect(library.define("f", SourceView{"echo old"}).ok(),
           "small-arena fixture installs");
    const auto old = library.find("f")->body.source.text();
    const auto refused = library.define(
        "f", SourceView{"echo replacement is too long"});
    expect(refused.status == Status::Overflow &&
               refused.overflow.storage_class ==
                   mm::shell::StorageClass::FunctionArena &&
               library.find("f")->body.source.text() == old,
           "failed replacement retains old body");
    expect(library.define("if", SourceView{"true"}).status ==
               Status::BadArgument &&
               library.define("shift", SourceView{"true"}).status ==
                   Status::BadArgument &&
               library.define("bad-name", SourceView{"true"}).status ==
                   Status::BadArgument,
           "reserved, special, and malformed names are rejected");
    expect(library.define("valid", SourceView{"if true"}).status ==
               Status::BadArgument && library.find("valid") == nullptr,
           "malformed body does not publish a function");
}

void parser_arena_preflight() {
    constexpr std::string_view body = "echo hi";
    const auto needed = mm::shell::measure_embedded(SourceView{body});
    expect(needed.status == mm::shell::ParseStatus::Complete &&
               needed.required.tokens > 0,
           "function fixture measures");
    Scratch scratch;
    auto storage = scratch.storage();
    storage.tokens = std::span{scratch.tokens}.first(
        needed.required.tokens - 1);
    FunctionLibrary library{storage};
    const auto short_result = library.define("f", SourceView{body});
    expect(short_result.status == Status::Overflow &&
               short_result.overflow.storage_class ==
                   mm::shell::StorageClass::FunctionArena &&
               short_result.overflow.required ==
                   needed.required.tokens && library.size() == 0,
           "one-short token arena refuses without publishing");
    storage.tokens = std::span{scratch.tokens}.first(
        needed.required.tokens);
    FunctionLibrary exact{storage};
    expect(exact.define("f", SourceView{body}).ok() &&
               exact.find("f")->body.tokens.size() ==
                   needed.required.tokens,
           "exact token arena admits the function");
}

void every_arena_is_preflighted() {
    constexpr std::string_view body = "echo hi";
    const auto needed = mm::shell::measure_embedded(SourceView{body});
    expect(needed.status == mm::shell::ParseStatus::Complete &&
               needed.required.fragments > 0 &&
               needed.required.nodes > 0 &&
               needed.required.links > 0 &&
               needed.required.context > 0,
           "all measured arenas are nonempty");
    Scratch scratch;
    for (int arena = 0; arena < 5; ++arena) {
        auto storage = scratch.storage();
        switch (arena) {
            case 0:
                storage.text = std::span{scratch.text}.first(
                    body.size());
                break;
            case 1:
                storage.fragments = std::span{scratch.fragments}.first(
                    needed.required.fragments - 1);
                break;
            case 2:
                storage.nodes = std::span{scratch.nodes}.first(
                    needed.required.nodes - 1);
                break;
            case 3:
                storage.links = std::span{scratch.links}.first(
                    needed.required.links - 1);
                break;
            case 4:
                storage.parser_context =
                    std::span{scratch.context}.first(
                        needed.required.context - 1);
                break;
        }
        FunctionLibrary library{storage};
        const auto result = library.define("f", SourceView{body});
        const auto expected = arena == 4 ?
            mm::shell::StorageClass::ParserContext :
            mm::shell::StorageClass::FunctionArena;
        expect(result.status == Status::Overflow &&
                   result.overflow.storage_class == expected &&
                   library.size() == 0 && library.find("f") == nullptr,
               "one-short function arena refuses atomically");
    }
}

const mm::test::case_ cases[]{
    {"deep copy and replacement", &deep_copy_and_replacement},
    {"replacement preflight and names",
     &replacement_preflight_and_names},
    {"parser arena preflight", &parser_arena_preflight},
    {"every arena is preflighted", &every_arena_is_preflighted},
};

const mm::test::registrar reg{"mm.shell functions", cases};

}  // namespace
