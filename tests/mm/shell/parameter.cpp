// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::ParameterKind;
using mm::shell::ParameterOperator;
using mm::shell::ParameterStatus;
using mm::shell::ParameterValueKind;
using mm::test::expect;

void names_specials_and_indices() {
    const auto name = mm::shell::parse_parameter("$HOME");
    expect(name.status == ParameterStatus::Ok &&
               name.parameter.kind == ParameterKind::Name &&
               name.parameter.name == "HOME",
           "unbraced named variable parses");
    const auto digit = mm::shell::parse_parameter("$1");
    expect(digit.status == ParameterStatus::Ok &&
               digit.parameter.kind == ParameterKind::Positional &&
               digit.parameter.index == 1,
           "unbraced digit is one positional index");
    const auto large = mm::shell::parse_parameter("${4294967295}");
    expect(large.status == ParameterStatus::Ok &&
               large.parameter.index == 4294967295U,
           "largest uint32 positional index parses");
    constexpr std::array special{
        ParameterKind::Count, ParameterKind::LastStatus,
        ParameterKind::ShellId, ParameterKind::Star, ParameterKind::At};
    constexpr std::array<std::string_view, 5> spellings{
        "$#", "$?", "$$", "$*", "$@"};
    for (std::size_t i = 0; i < spellings.size(); ++i) {
        const auto result = mm::shell::parse_parameter(spellings[i]);
        expect(result.status == ParameterStatus::Ok &&
                   result.parameter.kind == special[i],
               "special parameter spelling maps to its exact kind");
    }
}

void default_operators() {
    const auto fallback = mm::shell::parse_parameter("${A:-${B:-x}}");
    expect(fallback.status == ParameterStatus::Ok &&
               fallback.parameter.operation ==
                   ParameterOperator::Default &&
               fallback.parameter.name == "A" &&
               fallback.parameter.operand == "${B:-x}",
           "nested default operand remains a source view");
    const auto alternate = mm::shell::parse_parameter("${10:+yes}");
    expect(alternate.status == ParameterStatus::Ok &&
               alternate.parameter.operation ==
                   ParameterOperator::Alternate &&
               alternate.parameter.index == 10,
           "default operators admit positional references");
    const auto assign = mm::shell::parse_parameter("${A:=}");
    expect(assign.status == ParameterStatus::Ok &&
               assign.parameter.operation ==
                   ParameterOperator::Assign &&
               assign.parameter.operand.empty(),
           "named assignment accepts empty operand");
}

void malformed_and_overflow() {
    constexpr std::array<std::string_view, 7> malformed{
        "$10", "${01}", "${1:=x}", "${A:bad}",
        "${A", "${}", "${BAD-NAME}"};
    for (const auto spelling : malformed) {
        expect(mm::shell::parse_parameter(spelling).status ==
                   ParameterStatus::Syntax,
               "invalid parameter spelling is syntax error");
    }
    expect(mm::shell::parse_parameter("${4294967296}").status ==
               ParameterStatus::Range,
           "braced index beyond uint32 is refused");
}

void read_only_resolution() {
    mm::shell::VariableSlot variables[2]{};
    char variable_text[32]{};
    mm::shell::PositionalSlot positionals[3]{};
    char positional_text[16]{};
    mm::shell::ShellState state{variables, variable_text,
                                positionals, positional_text};
    state.last_status = 125;
    state.shell_id = 42;
    expect(state.assign("A", "").ok(), "empty variable installs");
    const auto fallback = mm::shell::parse_parameter("${A:-fallback}");
    const auto selected = mm::shell::resolve_parameter(
        fallback.parameter, state);
    expect(selected.status == mm::shell::Status::Ok &&
               selected.kind == ParameterValueKind::Operand &&
               selected.text == "fallback",
           "empty variable selects default operand without mutation");
    const auto assign = mm::shell::parse_parameter("${A:=new}");
    const auto pending = mm::shell::resolve_parameter(
        assign.parameter, state);
    expect(pending.kind == ParameterValueKind::AssignOperand &&
               state.lookup("A").value.empty(),
           "assignment decision does not mutate state");
    mm::shell::VariableSlot shadow_slots[2]{};
    char shadow_text[32]{};
    mm::shell::ShellState shadow;
    expect(state.fork_variables(shadow_slots, shadow_text, shadow).ok() &&
               shadow.assign("A", pending.text).ok(),
           "selected assignment stages in a caller-owned fork");
    expect(state.lookup("A").value.empty(),
           "staging a default assignment leaves live state untouched");
    const auto alternate = mm::shell::parse_parameter("${A:+other}");
    expect(mm::shell::resolve_parameter(alternate.parameter, state).kind ==
               ParameterValueKind::Empty,
           "alternate operator skips empty variable");
    const auto count = mm::shell::parse_parameter("$#");
    expect(mm::shell::resolve_parameter(count.parameter, state).number == 0,
           "count parameter is the numeric zero when no arguments exist");
    const auto status = mm::shell::parse_parameter("$?");
    expect(mm::shell::resolve_parameter(status.parameter, state).number ==
               125,
           "last-status parameter is numeric");
    const auto shell_id = mm::shell::parse_parameter("$$");
    expect(mm::shell::resolve_parameter(shell_id.parameter, state).number ==
               42,
           "shell identity comes from state");
    state.nounset = true;
    const auto missing = mm::shell::parse_parameter("$MISSING");
    expect(mm::shell::resolve_parameter(missing.parameter, state).status ==
               mm::shell::Status::NotFound,
           "nounset rejects an unguarded missing variable");
}

void vector_nullness() {
    mm::shell::VariableSlot variables[1]{};
    char variable_text[4]{};
    mm::shell::PositionalSlot positionals[3]{};
    char text[8]{};
    mm::shell::ShellState state{variables, variable_text,
                                positionals, text};
    constexpr std::array<std::string_view, 2> empty{"", ""};
    expect(state.set_positionals("f", empty).ok(),
           "two empty arguments install");
    const auto at = mm::shell::parse_parameter("${@:-fallback}");
    expect(mm::shell::resolve_parameter(at.parameter, state).kind ==
               ParameterValueKind::Arguments,
           "two empty at-arguments remain two fields");
    const auto star = mm::shell::parse_parameter("${*:-fallback}");
    expect(mm::shell::resolve_parameter(star.parameter, state).kind ==
               ParameterValueKind::Arguments,
           "default IFS joins two empty star-arguments nonempty");
    expect(state.assign("IFS", "").ok(),
           "empty IFS installs");
    const auto selected = mm::shell::resolve_parameter(
        star.parameter, state);
    expect(selected.kind == ParameterValueKind::Operand &&
               selected.text == "fallback",
           "empty IFS makes joined empty star-arguments null");
}

void materialized_field_pipeline() {
    mm::shell::VariableSlot variables[2]{};
    char variable_text[32]{};
    mm::shell::PositionalSlot positionals[4]{};
    char positional_text[24]{};
    mm::shell::ShellState state{variables, variable_text,
                                positionals, positional_text};
    expect(state.assign("A", "a b").ok(),
           "scalar parameter fixture installs");
    constexpr std::array<std::string_view, 3> arguments{
        "one", "", "three"};
    expect(state.set_positionals("f", arguments).ok(),
           "vector parameter fixture installs");
    mm::shell::FieldPiece pieces[8]{};
    char number_text[20]{};
    char output_text[32]{};
    mm::shell::SourceSpan output_fields[8]{};
    mm::shell::FieldView out;
    const auto name = mm::shell::parse_parameter("$A");
    const auto scalar = mm::shell::resolve_parameter(
        name.parameter, state);
    const auto materialized = mm::shell::materialize_parameter(
        scalar, state, false, pieces, number_text);
    expect(materialized.status == mm::shell::Status::Ok &&
               materialized.piece_count == 1,
           "scalar resolution emits one field piece");
    const auto split = mm::shell::split_fields(
        std::span{pieces}.first(materialized.piece_count), state.ifs(),
        {output_text, output_fields}, out);
    expect(split.status == mm::shell::Status::Ok &&
               out.fields.size() == 2 && out.field(0) == "a" &&
               out.field(1) == "b",
           "unquoted scalar flows through field splitting");
    const auto quoted = mm::shell::materialize_parameter(
        scalar, state, true, pieces, number_text);
    const auto single = mm::shell::split_fields(
        std::span{pieces}.first(quoted.piece_count), state.ifs(),
        {output_text, output_fields}, out);
    expect(single.status == mm::shell::Status::Ok &&
               out.fields.size() == 1 && out.field(0) == "a b",
           "quoted scalar bypasses field splitting");
    const auto at = mm::shell::parse_parameter("$@");
    const auto vector = mm::shell::resolve_parameter(at.parameter, state);
    const auto many = mm::shell::materialize_parameter(
        vector, state, true, pieces, number_text);
    const auto joined = mm::shell::split_fields(
        std::span{pieces}.first(many.piece_count), state.ifs(),
        {output_text, output_fields}, out);
    expect(joined.status == mm::shell::Status::Ok &&
               out.fields.size() == 3 && out.field(0) == "one" &&
               out.field(1).empty() && out.field(2) == "three",
           "quoted at flows through explicit field boundaries");
}

void materialization_preflight() {
    const mm::shell::ShellState state;
    const auto spec = mm::shell::parse_parameter("$?");
    const auto value = mm::shell::resolve_parameter(
        spec.parameter, state);
    mm::shell::FieldPiece piece{mm::shell::FieldPieceKind::Literal, "old"};
    char short_number[1]{'x'};
    const auto failed = mm::shell::materialize_parameter(
        value, state, false, std::span{&piece, 1},
        std::span<char>{short_number}.first(0));
    expect(failed.status == mm::shell::Status::Overflow &&
               failed.overflow.storage_class ==
                   mm::shell::StorageClass::ExpansionScratch &&
               failed.overflow.required == 1 &&
               piece.text == "old" && short_number[0] == 'x',
           "short numeric scratch leaves piece and bytes untouched");
    const auto star = mm::shell::parse_parameter("$*");
    const auto star_value = mm::shell::resolve_parameter(
        star.parameter, state);
    const auto quoted_star = mm::shell::materialize_parameter(
        star_value, state, true, std::span{&piece, 1}, short_number);
    expect(quoted_star.status == mm::shell::Status::Ok &&
               quoted_star.piece_count == 1 &&
               piece.kind == mm::shell::FieldPieceKind::Quoted &&
               piece.text.empty(),
           "quoted star with no arguments yields an empty field piece");
    const auto at = mm::shell::parse_parameter("$@");
    const auto at_value = mm::shell::resolve_parameter(at.parameter, state);
    const auto quoted_at = mm::shell::materialize_parameter(
        at_value, state, true, std::span{&piece, 1}, short_number);
    expect(quoted_at.status == mm::shell::Status::Ok &&
               quoted_at.piece_count == 0,
           "quoted at with no arguments yields no pieces");
}

const mm::test::case_ cases[]{
    {"names specials and indices", &names_specials_and_indices},
    {"default operators", &default_operators},
    {"malformed and overflow", &malformed_and_overflow},
    {"read-only resolution", &read_only_resolution},
    {"vector nullness", &vector_nullness},
    {"materialized field pipeline", &materialized_field_pipeline},
    {"materialization preflight", &materialization_preflight},
};

const mm::test::registrar reg{"mm.shell parameter", cases};

}  // namespace
