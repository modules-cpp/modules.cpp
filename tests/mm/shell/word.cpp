// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::FragmentKind;
using mm::shell::ScanStatus;
using mm::shell::SourceView;
using mm::shell::TokenKind;
using mm::shell::WordFragment;
using mm::test::expect;

void embedded_tokens_and_boundaries() {
    constexpr std::string_view source =
        "#!/bin/sh\r\nif test x && echo ok; then\n"
        "case x in a|b) echo yes;; esac\nfi\n";
    constexpr std::array expected{
        TokenKind::Newline, TokenKind::Word, TokenKind::Word,
        TokenKind::Word, TokenKind::AndIf, TokenKind::Word,
        TokenKind::Word, TokenKind::Semicolon, TokenKind::Word,
        TokenKind::Newline, TokenKind::Word, TokenKind::Word,
        TokenKind::Word, TokenKind::Word, TokenKind::CaseBar,
        TokenKind::Word, TokenKind::RightParen, TokenKind::Word,
        TokenKind::Word, TokenKind::DoubleSemicolon, TokenKind::Word,
        TokenKind::Newline, TokenKind::Word, TokenKind::Newline,
        TokenKind::End};
    std::size_t offset = 0;
    for (const auto kind : expected) {
        const auto result = mm::shell::scan_embedded(
            SourceView{source}, offset);
        expect(result.status == ScanStatus::Complete,
               "embedded source scans completely");
        expect(result.token.kind == kind, "token matches grammar spelling");
        expect(result.token.next_offset >= offset, "scanner advances");
        offset = result.token.next_offset;
    }
    expect(source.starts_with("#!/bin/sh\r\n"), "source remains immutable");
}

void quoted_and_expanded_fragments() {
    constexpr std::string_view source =
        "a'bc'\"$name\"\\ z${10}$((1+2))$(echo hi)";
    WordFragment fragments[9]{};
    const auto measured = mm::shell::scan_embedded(SourceView{source}, 0);
    expect(measured.status == ScanStatus::Complete,
           "mixed word measures completely");
    expect(measured.token.fragments_required == 8,
           "mixed word has eight fragments");
    const auto built = mm::shell::scan_embedded(SourceView{source}, 0,
                                               fragments);
    expect(built.status == ScanStatus::Complete,
           "mixed word builds completely");
    constexpr std::array kinds{
        FragmentKind::Literal, FragmentKind::SingleQuoted,
        FragmentKind::Parameter, FragmentKind::Escaped,
        FragmentKind::Literal, FragmentKind::Parameter,
        FragmentKind::Arithmetic,
        FragmentKind::CommandSubstitution};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        expect(fragments[i].kind == kinds[i], "fragment kind is preserved");
    }
    expect(fragments[2].quoted, "double-quoted parameter stays quoted");
    expect(!fragments[5].quoted, "unquoted parameter stays unquoted");
    expect(SourceView{source}.slice(fragments[7].source) == "$(echo hi)",
           "command substitution keeps its original source span");
}

void incomplete_malformed_and_overflow() {
    constexpr std::array incomplete{"'unterminated", "\"unfinished",
                                    "${name", "$(echo", "$((1+2)",
                                    "word\\"};
    for (const auto text : incomplete) {
        const auto result = mm::shell::scan_embedded(SourceView{text}, 0);
        expect(result.status == ScanStatus::Incomplete,
               "unfinished construct is incomplete");
    }
    constexpr std::array malformed{"a > b", "x &", "`date`", "x\r"};
    for (const auto text : malformed) {
        std::size_t offset = 0;
        bool refused = false;
        for (int step = 0; step < 4; ++step) {
            const auto result = mm::shell::scan_embedded(SourceView{text},
                                                         offset);
            if (result.status == ScanStatus::Malformed) {
                refused = true;
                break;
            }
            if (result.token.kind == TokenKind::End) break;
            offset = result.token.next_offset;
        }
        expect(refused, "deferred operator is malformed in embedded scan");
    }

    constexpr std::string_view nul_comment{"# bad\0comment", 13};
    const auto nul = mm::shell::scan_embedded(SourceView{nul_comment}, 0);
    expect(nul.status == ScanStatus::Malformed,
           "NUL in comment is malformed source");

    const auto case_bar = mm::shell::scan_embedded(SourceView{"|b"}, 0);
    expect(case_bar.status == ScanStatus::Complete &&
               case_bar.token.kind == TokenKind::CaseBar,
           "single bar is reserved for parser case context");

    WordFragment short_fragments[1]{
        {FragmentKind::Escaped, {91, 92}, true}};
    const auto result = mm::shell::scan_embedded(SourceView{"a'b'"}, 0,
                                                short_fragments);
    expect(result.status == ScanStatus::Overflow,
           "short fragment span reports overflow");
    expect(result.overflow.required == 2,
           "overflow reports exact fragment count");
    expect(short_fragments[0].source.offset == 91,
           "overflow leaves caller storage unchanged");
}

void independent_scanners() {
    const auto first = mm::shell::scan_embedded(SourceView{"echo first"}, 0);
    const auto second = mm::shell::scan_embedded(SourceView{"if true"}, 0);
    expect(first.token.kind == TokenKind::Word &&
               second.token.kind == TokenKind::Word,
           "scanner calls share no state");
    expect(first.token.source.length == 4 &&
               second.token.source.length == 2,
           "each scanner retains its own source span");
}

const mm::test::case_ cases[]{
    {"embedded tokens and boundaries", &embedded_tokens_and_boundaries},
    {"quoted and expanded fragments", &quoted_and_expanded_fragments},
    {"incomplete malformed and overflow", &incomplete_malformed_and_overflow},
    {"independent scanners", &independent_scanners},
};

const mm::test::registrar reg{"mm.shell scanner", cases};

}  // namespace
