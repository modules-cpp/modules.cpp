// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string_view>
#include <vector>

import mm.parse;
import mm.test;

namespace {

using mm::parse::TokenKind;
using mm::parse::FragmentKind;
using mm::parse::Dialect;
using mm::parse::Cursor;
using mm::parse::Sink;
using mm::parse::SourceSpan;
using mm::parse::ScanOutcome;
using mm::test::expect;

struct TestSink {
    std::vector<TokenKind> tokens;
    std::vector<std::pair<FragmentKind, SourceSpan>> fragments;
    std::size_t token_calls = 0;
    std::size_t fragment_calls = 0;
    bool last_quoted = false;
};

static void emit_token(void* ctx, TokenKind kind, SourceSpan span,
                        bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->tokens.push_back(kind);
    sink->last_quoted = quoted;
    ++sink->token_calls;
}

static void emit_fragment(void* ctx, FragmentKind kind, SourceSpan span,
                            bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->fragments.emplace_back(kind, span);
    sink->last_quoted = quoted;
    ++sink->fragment_calls;
}

static Sink make_sink(TestSink& ts) {
    return {&ts, emit_token, emit_fragment, false};
}

// --- Embedded dialect scanning tests ---

void test_embedded_literal_word() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"echo hello", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "literal word is Word");
    expect(result.span.offset == 0 && result.span.length == 4,
           "literal word span is {0, 4}");
    expect(result.next_offset == 4, "next_offset after literal word");
    expect(result.complete, "literal word is complete");
    expect(ts.token_calls == 1, "one token emitted");
    expect(ts.tokens[0] == TokenKind::Word, "emitted token is Word");
}

void test_embedded_single_quoted() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"'hello world'", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "quoted word is Word");
    expect(result.span.length == 13, "quoted word spans full quote");
    expect(ts.fragment_calls == 1, "one fragment emitted");
    expect(ts.fragments[0].first == FragmentKind::SingleQuoted,
           "fragment is SingleQuoted");
}

void test_embedded_double_quoted_with_param() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"\"$x\"", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "double-quoted is Word");
    expect(ts.fragment_calls >= 1, "fragments emitted for double-quoted");
    bool has_param = false;
    for (const auto& [kind, span] : ts.fragments) {
        if (kind == FragmentKind::Parameter) has_param = true;
    }
    expect(has_param, "contains Parameter fragment");
}

void test_embedded_operators() {
    struct {
        const char* text;
        TokenKind expected;
    } ops[] = {
        {"&&", TokenKind::AndIf},
        {"||", TokenKind::OrIf},
        {";;", TokenKind::DoubleSemicolon},
        {";", TokenKind::Semicolon},
        {"(", TokenKind::LeftParen},
        {")", TokenKind::RightParen},
        {"{", TokenKind::LeftBrace},
        {"}", TokenKind::RightBrace},
        {"!", TokenKind::Bang},
        {"|", TokenKind::CaseBar},
    };
    for (const auto& op : ops) {
        TestSink ts;
        Sink sink = make_sink(ts);
        Cursor cursor{op.text, 0, Dialect::Embedded};
        ScanOutcome result = cursor.scan(sink);
        expect(result.kind == op.expected, "operator token kind");
        expect(result.complete, "operator is complete");
    }
}

void test_embedded_newline() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"\n", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Newline, "LF is Newline");
    expect(result.next_offset == 1, "next_offset after LF");
}

void test_embedded_crlf() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\r\nbar", 0, Dialect::Embedded};
    const ScanOutcome prefix = cursor.scan(sink);
    expect(prefix.complete && prefix.kind == TokenKind::Word,
           "foo prefix scans successfully");
    Cursor cursor2{"\r\nbar", 0, Dialect::Embedded};
    Sink sink2 = make_sink(ts);
    ScanOutcome result = cursor2.scan(sink2);
    expect(result.kind == TokenKind::Newline, "CRLF is Newline");
    expect(result.span.length == 2, "CRLF spans 2 bytes");
    expect(result.next_offset == 2, "next_offset after CRLF");
}

void test_embedded_bare_cr() {
    TestSink ts;
    Sink sink = make_sink(ts);
    // A line ends at \n or \r\n; a \r without its \n is refused, as
    // mm.shell's level-1 grammar has always refused it.
    Cursor cursor{"\r", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "bare CR is refused");
    Cursor crlf{"\r\n", 0, Dialect::Embedded};
    ScanOutcome ended = crlf.scan(sink);
    expect(ended.kind == TokenKind::Newline && ended.span.length == 2,
           "CR LF is one Newline");
}

void test_embedded_malformed_ampersand() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"echo & foo", 0, Dialect::Embedded};
    const ScanOutcome prefix = cursor.scan(sink);
    expect(prefix.complete && prefix.kind == TokenKind::Word,
           "echo prefix scans successfully");
    Cursor cursor2{"& foo", 0, Dialect::Embedded};
    Sink sink2 = make_sink(ts);
    ScanOutcome result = cursor2.scan(sink2);
    expect(!result.complete, "standalone & is malformed in embedded");
}

void test_embedded_malformed_lt() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"< file", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "< is malformed in embedded");
}

void test_embedded_comment() {
    TestSink ts;
    Sink sink = make_sink(ts);
    // The comment is skipped; the newline that ends it is still a token.
    Cursor cursor{"# comment\nword", 0, Dialect::Embedded};
    ScanOutcome newline = cursor.scan(sink);
    expect(newline.kind == TokenKind::Newline && newline.span.offset == 9,
           "comment skipped, its line end returned");
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "word after the comment");
    expect(result.span.offset == 10, "word starts after comment");
}

void test_embedded_line_continuation() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\\\nbar", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "continued word is Word");
    expect(result.span.length == 8,
           "continued word spans foo, the continuation, and bar");
}

void test_embedded_command_substitution() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"$(cmd)", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "$() is Word");
    expect(ts.fragment_calls >= 1, "fragment emitted for $()");
    bool has_cmd_sub = false;
    for (const auto& [kind, span] : ts.fragments) {
        if (kind == FragmentKind::CommandSubstitution) has_cmd_sub = true;
    }
    expect(has_cmd_sub, "contains CommandSubstitution fragment");
}

void test_embedded_arithmetic() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"$((1+2))", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "$((..)) is Word");
    bool has_arith = false;
    for (const auto& [kind, span] : ts.fragments) {
        if (kind == FragmentKind::Arithmetic) has_arith = true;
    }
    expect(has_arith, "contains Arithmetic fragment");
}

void test_embedded_parameter() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"${var}", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "${..} is Word");
    bool has_param = false;
    for (const auto& [kind, span] : ts.fragments) {
        if (kind == FragmentKind::Parameter) has_param = true;
    }
    expect(has_param, "contains Parameter fragment");
}

void test_embedded_end_of_text() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::End, "empty input is End");
    expect(result.at_end, "at_end is true");
}

const mm::test::case_ cases[]{
    {"embedded literal word", &test_embedded_literal_word},
    {"embedded single quoted", &test_embedded_single_quoted},
    {"embedded double quoted with param", &test_embedded_double_quoted_with_param},
    {"embedded operators", &test_embedded_operators},
    {"embedded newline", &test_embedded_newline},
    {"embedded CRLF", &test_embedded_crlf},
    {"embedded bare CR", &test_embedded_bare_cr},
    {"embedded malformed ampersand", &test_embedded_malformed_ampersand},
    {"embedded malformed lt", &test_embedded_malformed_lt},
    {"embedded comment", &test_embedded_comment},
    {"embedded line continuation", &test_embedded_line_continuation},
    {"embedded command substitution", &test_embedded_command_substitution},
    {"embedded arithmetic", &test_embedded_arithmetic},
    {"embedded parameter", &test_embedded_parameter},
    {"embedded end of text", &test_embedded_end_of_text},
};

const mm::test::registrar reg{"mm.parse cursor embedded", cases};

}  // namespace
