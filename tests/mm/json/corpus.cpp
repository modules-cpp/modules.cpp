// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The JSON Parsing Test Suite under fixtures/: every y_ file parses except
// the ones a documented policy of this module refuses, every n_ file is
// refused, and every i_ file -- where the suite leaves the outcome to the
// implementation -- has the outcome this module chose, listed here so that
// a change to one is a change someone made on purpose.
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.json;
import mm.test;

namespace {

using mm::json::Status;
using mm::json::Value;
using mm::test::expect;

const std::filesystem::path fixtures = "tests/mm/json/fixtures";

[[nodiscard]] std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

struct Expected {
    std::string_view name;
    Status status;
};

// y_ files a default policy refuses. Both repeat an object key; the RFC
// recommends unique keys and this module rejects a repeat by default.
constexpr Expected y_exceptions[] = {
    {"y_object_duplicated_key.json", Status::DuplicateKey},
    {"y_object_duplicated_key_and_value.json", Status::DuplicateKey},
};

// i_ files, every one. The groups: unpaired or inverted surrogate escapes,
// and one in a key, are BadUnicode; malformed, overlong, truncated, and
// non-UTF-8 bytes inside a string are BadUnicode, while UTF-16 documents put
// a null or a 0xFF byte where the grammar wants a bracket and are Malformed
// there, before any string; an integer beyond long long is BadNumber under
// the default; a magnitude beyond double is BadNumber; a magnitude beneath
// it is Ok as zero; five hundred nested arrays are TooDeep; and a byte order
// mark before an empty object is skipped.
constexpr Expected i_outcomes[] = {
    {"i_number_double_huge_neg_exp.json", Status::Ok},
    {"i_number_huge_exp.json", Status::BadNumber},
    {"i_number_neg_int_huge_exp.json", Status::BadNumber},
    {"i_number_pos_double_huge_exp.json", Status::BadNumber},
    {"i_number_real_neg_overflow.json", Status::BadNumber},
    {"i_number_real_pos_overflow.json", Status::BadNumber},
    {"i_number_real_underflow.json", Status::Ok},
    {"i_number_too_big_neg_int.json", Status::BadNumber},
    {"i_number_too_big_pos_int.json", Status::BadNumber},
    {"i_number_very_big_negative_int.json", Status::BadNumber},
    {"i_object_key_lone_2nd_surrogate.json", Status::BadUnicode},
    {"i_string_1st_surrogate_but_2nd_missing.json", Status::BadUnicode},
    {"i_string_1st_valid_surrogate_2nd_invalid.json", Status::BadUnicode},
    {"i_string_UTF-16LE_with_BOM.json", Status::Malformed},
    {"i_string_UTF-8_invalid_sequence.json", Status::BadUnicode},
    {"i_string_UTF8_surrogate_U+D800.json", Status::BadUnicode},
    {"i_string_incomplete_surrogate_and_escape_valid.json", Status::BadUnicode},
    {"i_string_incomplete_surrogate_pair.json", Status::BadUnicode},
    {"i_string_incomplete_surrogates_escape_valid.json", Status::BadUnicode},
    {"i_string_invalid_lonely_surrogate.json", Status::BadUnicode},
    {"i_string_invalid_surrogate.json", Status::BadUnicode},
    {"i_string_invalid_utf-8.json", Status::BadUnicode},
    {"i_string_inverted_surrogates_U+1D11E.json", Status::BadUnicode},
    {"i_string_iso_latin_1.json", Status::BadUnicode},
    {"i_string_lone_second_surrogate.json", Status::BadUnicode},
    {"i_string_lone_utf8_continuation_byte.json", Status::BadUnicode},
    {"i_string_not_in_unicode_range.json", Status::BadUnicode},
    {"i_string_overlong_sequence_2_bytes.json", Status::BadUnicode},
    {"i_string_overlong_sequence_6_bytes.json", Status::BadUnicode},
    {"i_string_overlong_sequence_6_bytes_null.json", Status::BadUnicode},
    {"i_string_truncated-utf-8.json", Status::BadUnicode},
    {"i_string_utf16BE_no_BOM.json", Status::Malformed},
    {"i_string_utf16LE_no_BOM.json", Status::Malformed},
    {"i_structure_500_nested_arrays.json", Status::TooDeep},
    {"i_structure_UTF-8_BOM_empty_object.json", Status::Ok},
};

[[nodiscard]] const Expected* find(std::span<const Expected> table, std::string_view name) {
    for (const auto& entry : table)
        if (entry.name == name) return &entry;
    return nullptr;
}

[[nodiscard]] std::vector<std::filesystem::path> files_with(std::string_view prefix) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(fixtures, ec)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with(prefix) && name.ends_with(".json")) result.push_back(entry.path());
    }
    return result;
}

void accepted_documents() {
    const auto files = files_with("y_");
    expect(files.size() == 98, "the suite's ninety-eight y_ files are present");
    std::size_t exceptions = 0;
    for (const auto& file : files) {
        const auto name = file.filename().string();
        Value value;
        const auto outcome = mm::json::parse(read(file), value);
        if (const auto* exception = find(y_exceptions, name)) {
            ++exceptions;
            expect(outcome.status == exception->status, name + " is refused by policy");
            continue;
        }
        expect(outcome.status == Status::Ok, name + " parses");
        if (outcome.status != Status::Ok) continue;
        // A y_ file the module accepts round-trips through both layouts.
        std::string compact;
        std::string indented;
        Value again;
        expect(mm::json::write(value, compact).status == Status::Ok &&
                   mm::json::parse(compact, again).status == Status::Ok && again.equals(value) &&
                   mm::json::write(value, indented, mm::json::Layout::Indented).status ==
                       Status::Ok &&
                   mm::json::parse(indented, again).status == Status::Ok && again.equals(value),
               name + " round-trips");
    }
    expect(exceptions == sizeof(y_exceptions) / sizeof(y_exceptions[0]),
           "every listed y_ exception is present in the fixtures");
}

void refused_documents() {
    const auto files = files_with("n_");
    expect(files.size() == 219, "the suite's n_ files are present");
    for (const auto& file : files) {
        Value value;
        const auto outcome = mm::json::parse(read(file), value);
        expect(outcome.status != Status::Ok, file.filename().string() + " is refused");
    }
}

void implementation_defined_documents() {
    const auto files = files_with("i_");
    expect(files.size() == sizeof(i_outcomes) / sizeof(i_outcomes[0]),
           "every i_ file has a listed outcome");
    for (const auto& file : files) {
        const auto name = file.filename().string();
        const auto* expected = find(i_outcomes, name);
        expect(expected != nullptr, name + " is in the table");
        if (expected == nullptr) continue;
        Value value;
        const auto outcome = mm::json::parse(read(file), value);
        expect(outcome.status == expected->status, name + " has its listed outcome");
    }
}

const mm::test::case_ cases[] = {
    {"accepted documents", &accepted_documents},
    {"refused documents", &refused_documents},
    {"implementation-defined documents", &implementation_defined_documents},
};

const mm::test::registrar reg{"mm.json corpus", cases};

}
