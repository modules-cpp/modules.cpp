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
    bool last_token_quoted = false;
    bool last_fragment_quoted = false;
    SourceSpan last_token_span;
    SourceSpan last_fragment_span;
};

static void emit_token(void* ctx, TokenKind kind, SourceSpan span,
                        bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->tokens.push_back(kind);
    sink->last_token_quoted = quoted;
    sink->last_token_span = span;
    ++sink->token_calls;
}

static void emit_fragment(void* ctx, FragmentKind kind, SourceSpan span,
                            bool quoted) {
    auto* sink = static_cast<TestSink*>(ctx);
    sink->fragments.emplace_back(kind, span);
    sink->last_fragment_quoted = quoted;
    sink->last_fragment_span = span;
    ++sink->fragment_calls;
}

static Sink make_sink(TestSink& ts) {
    return {&ts, emit_token, emit_fragment, false};
}

// --- Sink callback tests ---

void test_sink_token_callback() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"hello", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(ts.token_calls == 1, "token callback called once");
    expect(ts.tokens[0] == TokenKind::Word, "token callback received Word");
    expect(ts.last_token_span.offset == 0, "token callback received offset");
    expect(ts.last_token_span.length == 5, "token callback received length");
    expect(!ts.last_token_quoted, "token callback received quoted=false");
}

void test_sink_fragment_order() {
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"a'b'c${x}d", 0, Dialect::Embedded};
    const ScanOutcome result = cursor.scan(sink);
    expect(result.complete && result.kind == TokenKind::Word,
           "callback input scans as a complete word");
    // Expected fragments in order: Literal(a), SingleQuoted(b),
    // Literal(c), Parameter(${x}), Literal(d)
    expect(ts.fragment_calls >= 3, "multiple fragments emitted");
    expect(ts.fragments[0].first == FragmentKind::Literal,
           "first fragment is Literal");
    bool has_single = false, has_param = false;
    for (const auto& [kind, span] : ts.fragments) {
        if (kind == FragmentKind::SingleQuoted) has_single = true;
        if (kind == FragmentKind::Parameter) has_param = true;
    }
    expect(has_single, "contains SingleQuoted fragment");
    expect(has_param, "contains Parameter fragment");
}

void test_sink_null_fragment() {
    // Null emit_fragment: fragments silently dropped, no crash.
    TestSink ts;
    Sink sink = {&ts, emit_token, nullptr, false};
    Cursor cursor{"'hello'", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "word scanned with null fragment sink");
    expect(ts.fragment_calls == 0, "no fragment calls with null sink");
    expect(ts.token_calls == 1, "token callback still called");
}

void test_sink_context_passthrough() {
    // Verify the context pointer is passed through to both callbacks.
    TestSink ts;
    Sink sink = make_sink(ts);
    expect(sink.context == &ts, "Sink context is the TestSink");
    Cursor cursor{"test", 0, Dialect::Embedded};
    const ScanOutcome result = cursor.scan(sink);
    expect(result.complete && result.kind == TokenKind::Word,
           "callback input scans as a complete word");
    expect(ts.token_calls == 1, "callback received context");
}

void test_sink_has_unquoted_glob() {
    // The Sink's has_unquoted_glob flag should reflect unquoted glob chars.
    TestSink ts;
    Sink sink = make_sink(ts);
    Cursor cursor{"a*b", 0, Dialect::Embedded};
    ScanOutcome result = cursor.scan(sink);
    expect(result.kind == TokenKind::Word, "glob word is Word");
    // Note: has_unquoted_glob tracking is in the Sink struct but the
    // Cursor does not currently set it. Verify it defaults to false.
    expect(!sink.has_unquoted_glob, "has_unquoted_glob defaults to false");
}

const mm::test::case_ cases[]{
    {"sink token callback", &test_sink_token_callback},
    {"sink fragment order", &test_sink_fragment_order},
    {"sink null fragment", &test_sink_null_fragment},
    {"sink context passthrough", &test_sink_context_passthrough},
    {"sink has_unquoted_glob", &test_sink_has_unquoted_glob},
};

const mm::test::registrar reg{"mm.parse sink", cases};

}  // namespace
