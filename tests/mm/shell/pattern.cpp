// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>

import mm.shell;
import mm.test;

namespace {

using mm::shell::PatternByte;
using mm::shell::Status;
using mm::test::expect;

void literal_and_wildcard_patterns() {
    constexpr std::array literal{PatternByte{'a'}, PatternByte{'b'}};
    expect(mm::shell::match_case_pattern(literal, "ab").matched,
           "literal case pattern matches exact text");
    expect(!mm::shell::match_case_pattern(literal, "abc").matched,
           "literal case pattern rejects suffix");
    constexpr std::array wildcard{
        PatternByte{'a'}, PatternByte{'*'}, PatternByte{'b'},
        PatternByte{'?'}};
    expect(mm::shell::match_case_pattern(wildcard, "axybz").matched,
           "star and question mark match without filesystem");
    expect(!mm::shell::match_case_pattern(wildcard, "ab").matched,
           "question mark requires one byte");
    constexpr std::array quoted{
        PatternByte{'*', true}, PatternByte{'?', true}};
    expect(mm::shell::match_case_pattern(quoted, "*?").matched,
           "quoted metacharacters are literal");
    expect(!mm::shell::match_case_pattern(quoted, "ab").matched,
           "quoted star cannot become wildcard");
}

void bracket_patterns() {
    constexpr std::array range{
        PatternByte{'['}, PatternByte{'a'}, PatternByte{'-'},
        PatternByte{'c'}, PatternByte{']'}};
    expect(mm::shell::match_case_pattern(range, "b").matched,
           "bracket range matches middle byte");
    expect(!mm::shell::match_case_pattern(range, "z").matched,
           "bracket range rejects outside byte");
    constexpr std::array negated{
        PatternByte{'['}, PatternByte{'!'}, PatternByte{'a'},
        PatternByte{']'}};
    expect(mm::shell::match_case_pattern(negated, "b").matched &&
               !mm::shell::match_case_pattern(negated, "a").matched,
           "negated bracket class works");
    constexpr std::array malformed{
        PatternByte{'['}, PatternByte{'z'}, PatternByte{'-'},
        PatternByte{'a'}, PatternByte{']'}};
    expect(mm::shell::match_case_pattern(malformed, "z").status ==
               Status::BadArgument,
           "reversed bracket range is invalid");
    constexpr std::array unclosed{PatternByte{'['}, PatternByte{'a'}};
    expect(mm::shell::match_case_pattern(unclosed, "a").status ==
               Status::BadArgument,
           "unclosed bracket expression is invalid");
    constexpr std::array high_bytes{
        PatternByte{'['}, PatternByte{static_cast<char>(0x80)},
        PatternByte{'-'}, PatternByte{static_cast<char>(0x9f)},
        PatternByte{']'}};
    expect(mm::shell::match_case_pattern(high_bytes, "\x90").matched,
           "range ordering uses unsigned bytes on every target");
}

const mm::test::case_ cases[]{
    {"literal and wildcard patterns", &literal_and_wildcard_patterns},
    {"bracket patterns", &bracket_patterns},
};

const mm::test::registrar reg{"mm.shell case pattern", cases};

}  // namespace
