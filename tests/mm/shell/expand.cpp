// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::FieldView;
using mm::shell::ShellState;
using mm::shell::SourceView;
using mm::shell::Status;
using mm::test::expect;

struct Scratch {
    mm::shell::WordFragment fragments[16]{};
    mm::shell::FieldPiece pieces[16]{};
    char generated[64]{};
    char field_text[64]{};
    mm::shell::SourceSpan fields[16]{};

    [[nodiscard]] mm::shell::WordExpansionStorage storage() {
        return {pieces, generated, {field_text, fields}};
    }
};

[[nodiscard]] mm::shell::WordExpansionResult expand(
    std::string_view word, const ShellState& state,
    Scratch& scratch, FieldView& out) {
    const auto scan = mm::shell::scan_embedded(
        SourceView{word}, 0, scratch.fragments);
    expect(scan.status == mm::shell::ScanStatus::Complete &&
               scan.token.kind == mm::shell::TokenKind::Word,
           "fixture scans as one word");
    return mm::shell::expand_word(
        SourceView{word},
        std::span{scratch.fragments}.first(
            scan.token.fragments_required),
        state, scratch.storage(), out);
}

void scalar_arithmetic_and_quotes() {
    mm::shell::VariableSlot variables[1]{};
    char variable_text[16]{};
    ShellState state{variables, variable_text, {}, {}};
    expect(state.assign("A", "a b").ok(), "variable installs");
    Scratch scratch;
    FieldView out;
    const auto unquoted = expand("pre${A}post", state, scratch, out);
    expect(unquoted.status == Status::Ok && out.fields.size() == 2 &&
               out.field(0) == "prea" && out.field(1) == "bpost",
           "unquoted parameter splits without splitting literal bytes");
    const auto quoted = expand("\"${A}\"", state, scratch, out);
    expect(quoted.status == Status::Ok && out.fields.size() == 1 &&
               out.field(0) == "a b",
           "quoted parameter remains one field");
    const auto arithmetic = expand("$((1+2*3))", state, scratch, out);
    expect(arithmetic.status == Status::Ok && out.fields.size() == 1 &&
               out.field(0) == "7",
           "arithmetic expansion uses checked evaluator");
    const auto empty_quote = expand("''", state, scratch, out);
    expect(empty_quote.status == Status::Ok && out.fields.size() == 1 &&
               out.field(0).empty(),
           "quoted empty word survives field splitting");
    const auto divide = expand("$((1/0))", state, scratch, out);
    expect(divide.status == Status::BadArgument &&
               divide.arithmetic_error ==
                   mm::shell::ArithmeticStatus::DivideByZero,
           "word expansion preserves arithmetic failure kind");
}

void positional_and_failure_atomicity() {
    mm::shell::PositionalSlot positionals[3]{};
    char positional_text[16]{};
    ShellState state{{}, {}, positionals, positional_text};
    constexpr std::array<std::string_view, 2> arguments{"one", ""};
    expect(state.set_positionals("f", arguments).ok(),
           "positionals install");
    Scratch scratch;
    FieldView out;
    const auto vector = expand("\"$@\"", state, scratch, out);
    expect(vector.status == Status::Ok && out.fields.size() == 2 &&
               out.field(0) == "one" && out.field(1).empty(),
           "quoted at retains empty positional argument");
    state.nounset = true;
    const auto previous = out.text;
    const auto missing = expand("$MISSING", state, scratch, out);
    expect(missing.status == Status::NotFound &&
               out.text == previous && out.fields.size() == 2,
           "nounset failure does not publish partial output");
    const auto deferred = expand("${MISSING:=x}", state, scratch, out);
    expect(deferred.status == Status::Unsupported &&
               out.text == previous && out.fields.size() == 2,
           "default assignment waits for transactional operand expansion");
}

void scratch_capacity() {
    const ShellState state;
    Scratch scratch;
    FieldView out{{"old"}, {}};
    const auto scan = mm::shell::scan_embedded(
        SourceView{"$((123))"}, 0, scratch.fragments);
    expect(scan.status == mm::shell::ScanStatus::Complete,
           "arithmetic fixture scans");
    auto storage = scratch.storage();
    storage.generated_text = std::span{scratch.generated}.first(2);
    const auto result = mm::shell::expand_word(
        SourceView{"$((123))"},
        std::span{scratch.fragments}.first(
            scan.token.fragments_required),
        state, storage, out);
    expect(result.status == Status::Overflow &&
               result.overflow.storage_class ==
                   mm::shell::StorageClass::ExpansionScratch &&
               result.overflow.required == 3 && out.text == "old",
           "one-short arithmetic scratch preserves published output");
}

const mm::test::case_ cases[]{
    {"scalar arithmetic and quotes", &scalar_arithmetic_and_quotes},
    {"positional and failure atomicity",
     &positional_and_failure_atomicity},
    {"scratch capacity", &scratch_capacity},
};

const mm::test::registrar reg{"mm.shell word expansion", cases};

}  // namespace
