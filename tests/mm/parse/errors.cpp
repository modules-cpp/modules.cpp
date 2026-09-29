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
    std::size_t token_calls = 0;
};

static void emit_token(void* ctx, TokenKind kind, SourceSpan span,
                        bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->tokens.push_back(kind);
    ++sink->token_calls;
}

static void emit_fragment(void* ctx, FragmentKind kind, SourceSpan span,
                            bool quoted) {
    (void)ctx;
    (void)kind;
    (void)span;
    (void)quoted;
}

static Sink make_sink(TestSink& ts) {
    return {&ts, emit_token, emit_fragment, false};
}

// --- Error recovery tests ---

void test_nul_byte() {
    TestSink ts;
    Sink sink = make_sink(ts);
    std::string_view text("foo\0bar", 7);
    // A NUL inside a word refuses the word, as mm.shell's level-1 scanner
    // always has.
    Cursor cursor{text, 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete && result.message == "NUL in script",
           "word containing NUL is refused");
    Cursor cursor2{text, 3, Dialect::Embedded};
    Sink sink2 = make_sink(ts);
    ScanOutcome result2 = cursor2.scan(sink2);
    expect(!result2.complete, "NUL byte is malformed");
}

void test_trailing_backslash() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\\", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "trailing backslash is malformed");
    expect(result.message == "trailing escape", "trailing escape message");
}

void test_unclosed_single_quote() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"'hello", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "unclosed quote is malformed");
    expect(result.message == "unclosed quote", "unclosed quote message");
}

void test_unclosed_double_quote() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"\"hello", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "unclosed double quote is malformed");
    expect(result.message == "unclosed quote", "unclosed quote message");
}

void test_unclosed_command_sub() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"$(cmd", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "unclosed $() is malformed");
    expect(result.message == "unclosed command or arithmetic substitution",
           "unclosed $() message");
}

void test_unclosed_arithmetic() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"$((1+2", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "unclosed $((..)) is malformed");
    expect(result.message == "unclosed command or arithmetic substitution",
           "unclosed arithmetic message");
}

void test_unclosed_parameter() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"${var", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "unclosed ${..} is malformed");
    expect(result.message == "unclosed parameter expansion",
           "unclosed parameter message");
}

void test_embedded_bare_cr_at_end() {
    // Embedded dialect: bare \r without \n is malformed.
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\r", 0, Dialect::Embedded};
    cursor.scan(sink);  // scan "foo"
    Cursor cursor2{"\r", 0, Dialect::Embedded};
    Sink sink2 = make_sink(ts);
    ScanOutcome result = cursor2.scan(sink2);
    // A \r at the end of the text has no \n to complete it.
    expect(!result.complete, "bare CR at end is refused");
}

void test_full_cr_in_word() {
    // Full dialect: \r is blank, so it stops a word.
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\rbar", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "word before CR");
    expect(result.span.length == 3, "word is foo (3 bytes)");
    ScanOutcome result2 = cursor.scan(sink);
    expect(result2.kind == TokenKind::Word, "word after CR");
    expect(result2.span.offset == 4, "second word starts after CR");
}

void test_full_gt_at_end() {
    // Full dialect: > at end of text is Output.
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"echo >", 0, Dialect::Full};
    cursor.scan(sink);  // scan "echo"
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Output, "> is Output");
    ScanOutcome end = cursor.scan(sink);
    expect(end.kind == TokenKind::End, "End after Output");
    expect(end.at_end, "at_end is true");
}

const mm::test::case_ cases[]{
    {"NUL byte", &test_nul_byte},
    {"trailing backslash", &test_trailing_backslash},
    {"unclosed single quote", &test_unclosed_single_quote},
    {"unclosed double quote", &test_unclosed_double_quote},
    {"unclosed command substitution", &test_unclosed_command_sub},
    {"unclosed arithmetic", &test_unclosed_arithmetic},
    {"unclosed parameter", &test_unclosed_parameter},
    {"embedded bare CR at end", &test_embedded_bare_cr_at_end},
    {"full CR in word", &test_full_cr_in_word},
    {"full > at end", &test_full_gt_at_end},
};

const mm::test::registrar reg{"mm.parse errors", cases};

}  // namespace
