// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::ParameterKind;
using mm::shell::ParameterOperator;
using mm::shell::ParameterStatus;
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

const mm::test::case_ cases[]{
    {"names specials and indices", &names_specials_and_indices},
    {"default operators", &default_operators},
    {"malformed and overflow", &malformed_and_overflow},
};

const mm::test::registrar reg{"mm.shell parameter", cases};

}  // namespace
