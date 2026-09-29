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
    bool last_quoted = false;
};

static void emit_token(void* ctx, TokenKind kind, SourceSpan span,
                        bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->tokens.push_back(kind);
    sink->last_quoted = quoted;
    ++sink->token_calls;
}

static Sink make_sink(TestSink& ts) {
    return {&ts, emit_token, nullptr, false};
}

// --- Full dialect scanning tests ---

void test_full_pipe() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"|", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Pipe, "single | is Pipe in full");
    expect(result.complete, "pipe is complete");
}

void test_full_redirections() {
    struct {
        const char* text;
        TokenKind expected;
    } ops[] = {
        {"<", TokenKind::Input},
        {">", TokenKind::Output},
        {">>", TokenKind::Append},
        {"<<", TokenKind::HereDocument},
        {"<&", TokenKind::DuplicateInput},
        {">&", TokenKind::DuplicateOutput},
    };
    for (const auto& op : ops) {
        TestSink ts;
        Sink sink = make_sink(ts);
        Cursor cursor{op.text, 0, Dialect::Full};
        ScanOutcome result = cursor.scan(sink);
        expect(result.kind == op.expected, "redirection token kind");
        expect(result.complete, "redirection is complete");
    }
}

void test_full_heredoc_variant_rejected() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"<<-", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "<<- rejected");
    expect(result.message == "here-document variant is not supported",
           "heredoc variant message");
}

void test_full_heredoc_triple_rejected() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"<<<", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "<<< rejected");
    expect(result.message == "here-document variant is not supported",
           "heredoc triple message");
}

void test_full_io_number() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"2>", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::IoNumber, "digit+> is IoNumber");
}

void test_full_not_io_number() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"2x", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "digit+char is Word");
}

void test_full_parens() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"(", 0, Dialect::Full};
    ScanOutcome open = cursor.scan(sink);
    expect(open.kind == TokenKind::LeftParen, "( is OpenParen");

    TestSink ts2;
    Sink sink2 = make_sink(ts2);
    Cursor cursor2{")", 0, Dialect::Full};
    ScanOutcome close = cursor2.scan(sink2);
    expect(close.kind == TokenKind::RightParen, ") is CloseParen");
}

void test_full_braces() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"{", 0, Dialect::Full};
    ScanOutcome open = cursor.scan(sink);
    expect(open.kind == TokenKind::LeftBrace, "{ is LeftBrace");

    TestSink ts2;
    Sink sink2 = make_sink(ts2);
    Cursor cursor2{"}", 0, Dialect::Full};
    ScanOutcome close = cursor2.scan(sink2);
    expect(close.kind == TokenKind::RightBrace, "} is RightBrace");
}

void test_full_double_paren_rejected() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"((", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "(( rejected");
    expect(result.message == "excluded shell construct", "excluded message");
}

void test_full_async_rejected() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"&", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "standalone & rejected");
    expect(result.message == "asynchronous execution is not supported",
           "async message");
}

void test_full_cr_is_blank() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\rbar", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "word stops at CR");
    expect(result.span.length == 3, "word is foo (3 bytes)");
    // Next scan should return "bar" as a new word.
    ScanOutcome result2 = cursor.scan(sink);
    expect(result2.kind == TokenKind::Word, "next word after CR");
    expect(result2.span.length == 3, "next word is bar (3 bytes)");
}

void test_full_double_bracket_rejected() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"[[", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "[[ rejected");
    expect(result.message == "excluded shell construct", "excluded message");
}

void test_full_backtick_rejected() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"`cmd`", 0, Dialect::Full};
    ScanOutcome result = cursor.scan(sink);
    expect(!result.complete, "backtick rejected");
    expect(result.message == "backtick substitution is not supported",
           "backtick message");
}

void test_full_cr_lf_separate() {
    // In the full dialect, \r is blank and \n is a newline. They are
    // separate, not a combined line ending.
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"foo\r\nbar", 0, Dialect::Full};
    ScanOutcome word = cursor.scan(sink);
    expect(word.kind == TokenKind::Word, "foo is a word");
    ScanOutcome nl = cursor.scan(sink);
    expect(nl.kind == TokenKind::Newline, "\\n after \\r is Newline");
    expect(nl.span.length == 1, "newline spans 1 byte (just \\n)");
}

const mm::test::case_ cases[]{
    {"full pipe", &test_full_pipe},
    {"full redirections", &test_full_redirections},
    {"full heredoc variant rejected", &test_full_heredoc_variant_rejected},
    {"full heredoc triple rejected", &test_full_heredoc_triple_rejected},
    {"full IoNumber", &test_full_io_number},
    {"full not IoNumber", &test_full_not_io_number},
    {"full parens", &test_full_parens},
    {"full braces", &test_full_braces},
    {"full double paren rejected", &test_full_double_paren_rejected},
    {"full async rejected", &test_full_async_rejected},
    {"full CR is blank", &test_full_cr_is_blank},
    {"full double bracket rejected", &test_full_double_bracket_rejected},
    {"full backtick rejected", &test_full_backtick_rejected},
    {"full CR LF separate", &test_full_cr_lf_separate},
};

const mm::test::registrar reg{"mm.parse cursor full", cases};

}  // namespace
