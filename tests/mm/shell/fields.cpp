// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::FieldPiece;
using mm::shell::FieldPieceKind;
using mm::shell::FieldView;
using mm::shell::Status;
using mm::shell::StorageClass;
using mm::test::expect;

void splitting_and_quoting() {
    constexpr std::array pieces{
        FieldPiece{FieldPieceKind::Literal, "pre"},
        FieldPiece{FieldPieceKind::Split, " a b "},
        FieldPiece{FieldPieceKind::Literal, "post"}};
    char text[16]{};
    mm::shell::SourceSpan fields[4]{};
    FieldView out;
    const auto result = mm::shell::split_fields(
        pieces, " \t\n", {text, fields}, out);
    expect(result.status == Status::Ok && out.fields.size() == 4,
           "unquoted expansion splits around literal pieces");
    expect(out.field(0) == "pre" && out.field(1) == "a" &&
               out.field(2) == "b" && out.field(3) == "post",
           "split values retain literal attachment boundaries");

    constexpr std::array quoted{
        FieldPiece{FieldPieceKind::Quoted, ""}};
    FieldView empty;
    expect(mm::shell::split_fields(quoted, " \t\n",
                                    {text, fields}, empty).status ==
               Status::Ok &&
               empty.fields.size() == 1 && empty.field(0).empty(),
           "quoted empty word yields one empty field");
    constexpr std::array unquoted{
        FieldPiece{FieldPieceKind::Split, ""}};
    FieldView absent;
    expect(mm::shell::split_fields(unquoted, " \t\n",
                                    {text, fields}, absent).status ==
               Status::Ok && absent.fields.empty(),
           "unquoted empty expansion yields no field");
}

void quoted_at_boundaries() {
    constexpr std::array pieces{
        FieldPiece{FieldPieceKind::Literal, "pre"},
        FieldPiece{FieldPieceKind::Quoted, "one"},
        FieldPiece{FieldPieceKind::Boundary, {}},
        FieldPiece{FieldPieceKind::Quoted, ""},
        FieldPiece{FieldPieceKind::Boundary, {}},
        FieldPiece{FieldPieceKind::Quoted, "three"},
        FieldPiece{FieldPieceKind::Literal, "post"}};
    char text[32]{};
    mm::shell::SourceSpan fields[4]{};
    FieldView out;
    const auto result = mm::shell::split_fields(
        pieces, " \t\n", {text, fields}, out);
    expect(result.status == Status::Ok && out.fields.size() == 3,
           "quoted at supplies one field per positional argument");
    expect(out.field(0) == "preone" && out.field(1).empty() &&
               out.field(2) == "threepost",
           "prefix and suffix attach to outer arguments");
}

void mixed_ifs_delimiters() {
    constexpr std::array pieces{
        FieldPiece{FieldPieceKind::Split, "a , b"}};
    char text[8]{};
    mm::shell::SourceSpan fields[4]{};
    FieldView out;
    const auto result = mm::shell::split_fields(
        pieces, ", ", {text, fields}, out);
    expect(result.status == Status::Ok && out.fields.size() == 2 &&
               out.field(0) == "a" && out.field(1) == "b",
           "IFS whitespace adjacent to comma is one delimiter");
    constexpr std::array consecutive{
        FieldPiece{FieldPieceKind::Split, ",a,,b,"}};
    const auto second = mm::shell::split_fields(
        consecutive, ",", {text, fields}, out);
    expect(second.status == Status::Ok && out.fields.size() == 4 &&
               out.field(0).empty() && out.field(1) == "a" &&
               out.field(2).empty() && out.field(3) == "b",
           "nonwhitespace IFS preserves interior empty fields");
}

void exact_capacity_and_atomic_output() {
    constexpr std::array pieces{
        FieldPiece{FieldPieceKind::Quoted, "ab"},
        FieldPiece{FieldPieceKind::Boundary, {}},
        FieldPiece{FieldPieceKind::Quoted, "cd"}};
    char text[4]{'x', 'x', 'x', 'x'};
    mm::shell::SourceSpan fields[2]{{91, 92}, {91, 92}};
    FieldView out{{"old"}, {}};
    const auto bytes = mm::shell::split_fields(
        pieces, " ", {std::span<char>{text}.first(3), fields}, out);
    expect(bytes.status == Status::Overflow &&
               bytes.overflow.storage_class ==
                   StorageClass::ExpandedFieldText &&
               bytes.overflow.required == 4,
           "one-short text reports exact byte need");
    expect(text[0] == 'x' && fields[0].offset == 91 &&
               out.text == "old",
           "short text leaves outputs untouched");
    const auto slots = mm::shell::split_fields(
        pieces, " ", {text, std::span{fields}.first(1)}, out);
    expect(slots.status == Status::Overflow &&
               slots.overflow.storage_class ==
                   StorageClass::ExpandedFields &&
               slots.overflow.required == 2,
           "one-short field slots report exact count");
    expect(text[0] == 'x' && fields[0].offset == 91 &&
               out.text == "old",
           "short field span leaves outputs untouched");
}

const mm::test::case_ cases[]{
    {"splitting and quoting", &splitting_and_quoting},
    {"quoted at boundaries", &quoted_at_boundaries},
    {"mixed IFS delimiters", &mixed_ifs_delimiters},
    {"exact capacity and atomic output", &exact_capacity_and_atomic_output},
};

const mm::test::registrar reg{"mm.shell fields", cases};

}  // namespace
