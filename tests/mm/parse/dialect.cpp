// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string_view>
#include <vector>

import mm.parse;
import mm.test;

namespace {

using mm::parse::TokenKind;
using mm::parse::FragmentKind;
using mm::test::expect;

// Test harness: captures tokens and fragments emitted by the Cursor.
struct TestSink {
    std::vector<TokenKind> tokens;
    std::vector<std::pair<FragmentKind, mm::parse::SourceSpan>> fragments;
    std::size_t token_calls = 0;
    std::size_t fragment_calls = 0;
    bool last_quoted = false;
};

static void test_emit_token(void* ctx, TokenKind kind,
                              mm::parse::SourceSpan span, bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->tokens.push_back(kind);
    sink->last_quoted = quoted;
    ++sink->token_calls;
}

static void test_emit_fragment(void* ctx, FragmentKind kind,
                                 mm::parse::SourceSpan span, bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->fragments.emplace_back(kind, span);
    sink->last_quoted = quoted;
    ++sink->fragment_calls;
}

static mm::parse::Sink make_sink(TestSink& ts) {
    return {&ts, test_emit_token, test_emit_fragment, false};
}

// --- Predicate tests ---

void test_blank() {
    expect(mm::parse::blank(' '), "space is blank");
    expect(mm::parse::blank('\t'), "tab is blank");
    expect(mm::parse::blank('\r'), "CR is blank");
    expect(!mm::parse::blank('\n'), "LF is not blank");
    expect(!mm::parse::blank('a'), "letter is not blank");
    expect(!mm::parse::blank(';'), "operator is not blank");
}

void test_line_end() {
    expect(mm::parse::line_end('\n'), "LF is line end");
    expect(mm::parse::line_end('\r'), "CR is line end");
    expect(!mm::parse::line_end(' '), "space is not line end");
    expect(!mm::parse::line_end('\t'), "tab is not line end");
    expect(!mm::parse::line_end('a'), "letter is not line end");
}

void test_operator_start() {
    const char ops[] = {'&', '|', ';', '(', ')', '<', '>'};
    for (const char c : ops) {
        expect(mm::parse::operator_start(c), "operator is operator_start");
    }
    expect(!mm::parse::operator_start('a'), "letter is not operator_start");
    expect(!mm::parse::operator_start(' '), "space is not operator_start");
    expect(!mm::parse::operator_start('!'), "bang is not operator_start");
    expect(!mm::parse::operator_start('{'), "brace is not operator_start");
}

void test_brace_operator() {
    constexpr std::string_view text1 = "{ }";
    expect(mm::parse::brace_operator(text1, 0), "brace followed by space");

    constexpr std::string_view text2 = "{\n}";
    expect(mm::parse::brace_operator(text2, 0), "brace followed by newline");

    constexpr std::string_view text3 = "{;";
    expect(mm::parse::brace_operator(text3, 0), "brace followed by semicolon");

    constexpr std::string_view text4 = "{x";
    expect(!mm::parse::brace_operator(text4, 0), "brace followed by identifier");

    constexpr std::string_view text5 = "{";
    expect(mm::parse::brace_operator(text5, 0), "brace at end of text");

    constexpr std::string_view text6 = "a{";
    expect(!mm::parse::brace_operator(text6, 0), "not a brace character");
}

void test_is_continuation() {
    constexpr std::string_view text1 = "foo\\\nbar";
    expect(mm::parse::is_continuation(text1, 3), "backslash+LF is continuation");

    constexpr std::string_view text2 = "foo\\\r\nbar";
    expect(mm::parse::is_continuation(text2, 3), "backslash+CRLF is continuation");

    constexpr std::string_view text3 = "foo\\x";
    expect(!mm::parse::is_continuation(text3, 3), "backslash+char is not continuation");

    constexpr std::string_view text4 = "foo\\";
    expect(!mm::parse::is_continuation(text4, 3), "backslash at end is not continuation");
}

// --- Enum ordinal tests ---

void test_token_kind_count() {
    // The superset has exactly 21 values.
    constexpr TokenKind all[] = {
        TokenKind::End, TokenKind::Newline, TokenKind::Word,
        TokenKind::AndIf, TokenKind::OrIf, TokenKind::Semicolon,
        TokenKind::DoubleSemicolon, TokenKind::LeftParen,
        TokenKind::RightParen, TokenKind::LeftBrace, TokenKind::RightBrace,
        TokenKind::Bang, TokenKind::CaseBar, TokenKind::Pipe,
        TokenKind::Input, TokenKind::Output, TokenKind::Append,
        TokenKind::HereDocument, TokenKind::DuplicateInput,
        TokenKind::DuplicateOutput, TokenKind::IoNumber,
    };
    expect(sizeof(all) / sizeof(all[0]) == 21, "TokenKind has 21 values");
}

void test_fragment_kind_count() {
    constexpr FragmentKind all[] = {
        FragmentKind::Literal, FragmentKind::SingleQuoted,
        FragmentKind::DoubleQuoted, FragmentKind::Escaped,
        FragmentKind::Parameter, FragmentKind::Arithmetic,
        FragmentKind::CommandSubstitution,
    };
    expect(sizeof(all) / sizeof(all[0]) == 7, "FragmentKind has 7 values");
}

// --- Sink default tests ---

void test_sink_defaults() {
    mm::parse::Sink sink;
    expect(sink.context == nullptr, "Sink context defaults to null");
    expect(sink.emit_token == nullptr, "Sink emit_token defaults to null");
    expect(sink.emit_fragment == nullptr, "Sink emit_fragment defaults to null");
    expect(!sink.has_unquoted_glob, "Sink has_unquoted_glob defaults to false");
}

// --- SourceSpan tests ---

void test_source_span_defaults() {
    mm::parse::SourceSpan span;
    expect(span.offset == 0, "SourceSpan offset defaults to 0");
    expect(span.length == 0, "SourceSpan length defaults to 0");
}

void test_dialect_values() {
    expect(static_cast<int>(mm::parse::Dialect::Embedded) == 0,
           "Dialect::Embedded is ordinal 0");
    expect(static_cast<int>(mm::parse::Dialect::Full) == 1,
           "Dialect::Full is ordinal 1");
}

const mm::test::case_ cases[]{
    {"predicates blank", &test_blank},
    {"predicates line_end", &test_line_end},
    {"predicates operator_start", &test_operator_start},
    {"predicates brace_operator", &test_brace_operator},
    {"predicates is_continuation", &test_is_continuation},
    {"TokenKind has 21 values", &test_token_kind_count},
    {"FragmentKind has 7 values", &test_fragment_kind_count},
    {"Sink defaults", &test_sink_defaults},
    {"SourceSpan defaults", &test_source_span_defaults},
    {"Dialect ordinal values", &test_dialect_values},
};

const mm::test::registrar reg{"mm.parse dialect", cases};

}  // namespace
