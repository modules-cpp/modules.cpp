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
    mm::shell::VariableSlot shadow_variables[8]{};
    char shadow_text[128]{};

    [[nodiscard]] mm::shell::WordExpansionStorage storage() {
        return {pieces, generated, {field_text, fields},
                shadow_variables, shadow_text};
    }
};

[[nodiscard]] mm::shell::WordExpansionResult expand(
    std::string_view word, ShellState& state,
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
    const auto assigned = expand("${MISSING:=x}", state, scratch, out);
    expect(assigned.status == Status::Overflow &&
               assigned.overflow.storage_class ==
                   mm::shell::StorageClass::Variables &&
               !state.lookup("MISSING").found && out.text == previous,
           "assignment cannot exceed the parent's variable capacity");
}

void scratch_capacity() {
    ShellState state;
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

void literal_operands_and_transaction() {
    mm::shell::VariableSlot variables[3]{};
    char variable_text[32]{};
    ShellState state{variables, variable_text, {}, {}};
    Scratch scratch;
    FieldView out;
    state.nounset = true;
    const auto fallback = expand("${A:-one two}", state, scratch, out);
    expect(fallback.status == Status::Ok &&
               out.fields.size() == 2 && out.field(0) == "one" &&
               out.field(1) == "two" && !state.lookup("A").found,
           "default operand splits without assigning under nounset");
    const auto quote = expand("\"${A:-one two}\"", state, scratch, out);
    expect(quote.status == Status::Ok && out.fields.size() == 1 &&
               out.field(0) == "one two",
           "quoted default operand remains one field");
    expect(state.assign("B", "set").ok(), "alternate fixture installs");
    const auto alternate = expand("${B:+yes}", state, scratch, out);
    expect(alternate.status == Status::Ok && out.fields.size() == 1 &&
               out.field(0) == "yes",
           "alternate operand appears only for nonempty value");
    const auto absent = expand("${A:+no}", state, scratch, out);
    expect(absent.status == Status::Ok && out.fields.empty(),
           "absent alternate produces no field");
    const auto assignment = expand("${A:=first}${A}",
                                   state, scratch, out);
    expect(assignment.status == Status::Ok &&
               out.fields.size() == 1 && out.field(0) == "firstfirst" &&
               state.lookup("A").value == "first",
           "later fragments observe staged assignment");
}

void assignment_failure_preserves_state() {
    mm::shell::VariableSlot variables[1]{};
    char variable_text[8]{};
    ShellState state{variables, variable_text, {}, {}};
    Scratch scratch;
    FieldView out{{"old"}, {}};
    const auto unsupported = expand("${A:=${B:-x}}", state,
                                    scratch, out);
    expect(unsupported.status == Status::Unsupported &&
               !state.lookup("A").found && out.text == "old",
           "nested operand is explicitly deferred without mutation");
    const auto scan = mm::shell::scan_embedded(
        SourceView{"${A:=long}"}, 0, scratch.fragments);
    expect(scan.status == mm::shell::ScanStatus::Complete,
           "assignment fixture scans");
    auto storage = scratch.storage();
    storage.fields.text = std::span{scratch.field_text}.first(2);
    const auto overflow = mm::shell::expand_word(
        SourceView{"${A:=long}"},
        std::span{scratch.fragments}.first(
            scan.token.fragments_required),
        state, storage, out);
    expect(overflow.status == Status::Overflow &&
               !state.lookup("A").found && out.text == "old",
           "field overflow rolls back staged assignment");
    const auto assigned = expand("${A:=ok}", state, scratch, out);
    expect(assigned.status == Status::Ok &&
               state.lookup("A").value == "ok" &&
               out.field(0) == "ok",
           "assignment publishes state after field preflight");
}

void staged_values_survive_pool_compaction() {
    mm::shell::VariableSlot variables[2]{};
    char variable_text[32]{};
    ShellState state{variables, variable_text, {}, {}};
    expect(state.assign("A", "").ok() &&
               state.assign("B", "middle").ok(),
           "compaction fixture installs");
    Scratch scratch;
    FieldView out;
    const auto result = expand("${B}${A:=long}", state,
                               scratch, out);
    expect(result.status == Status::Ok &&
               out.fields.size() == 1 &&
               out.field(0) == "middlelong" &&
               state.lookup("B").value == "middle",
           "earlier expansion survives later shadow pool movement");
}

const mm::test::case_ cases[]{
    {"scalar arithmetic and quotes", &scalar_arithmetic_and_quotes},
    {"positional and failure atomicity",
     &positional_and_failure_atomicity},
    {"scratch capacity", &scratch_capacity},
    {"literal operands and transaction",
     &literal_operands_and_transaction},
    {"assignment failure preserves state",
     &assignment_failure_preserves_state},
    {"staged values survive pool compaction",
     &staged_values_survive_pool_compaction},
};

const mm::test::registrar reg{"mm.shell word expansion", cases};

}  // namespace
