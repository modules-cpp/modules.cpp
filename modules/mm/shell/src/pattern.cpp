// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :pattern;
import :status;

namespace mm::shell {
namespace {

struct AtomResult {
    Status status = Status::Ok;
    bool matched = false;
    std::size_t next = 0;
};

[[nodiscard]] AtomResult bracket(std::span<const PatternByte> pattern,
                                  std::size_t start, char value) {
    auto at = start + 1;
    bool negate = false;
    if (at < pattern.size() && !pattern[at].quoted &&
        (pattern[at].value == '!' || pattern[at].value == '^')) {
        negate = true;
        ++at;
    }
    bool matched = false;
    bool had_member = false;
    if (at < pattern.size() && !pattern[at].quoted &&
        pattern[at].value == ']') {
        matched = value == ']';
        had_member = true;
        ++at;
    }
    while (at < pattern.size()) {
        if (!pattern[at].quoted && pattern[at].value == ']' &&
            had_member) {
            return {Status::Ok, negate ? !matched : matched, at + 1};
        }
        const auto first = pattern[at].value;
        had_member = true;
        if (at + 2 < pattern.size() && !pattern[at + 1].quoted &&
            pattern[at + 1].value == '-' &&
            (pattern[at + 2].quoted || pattern[at + 2].value != ']')) {
            const auto last = pattern[at + 2].value;
            const auto first_byte = static_cast<unsigned char>(first);
            const auto last_byte = static_cast<unsigned char>(last);
            const auto value_byte = static_cast<unsigned char>(value);
            if (first_byte > last_byte) return {Status::BadArgument};
            if (value_byte >= first_byte && value_byte <= last_byte) {
                matched = true;
            }
            at += 3;
        } else {
            if (value == first) matched = true;
            ++at;
        }
    }
    return {Status::BadArgument};
}

[[nodiscard]] AtomResult match_atom(
    std::span<const PatternByte> pattern,
    std::size_t at, char value) {
    const auto atom = pattern[at];
    if (!atom.quoted && atom.value == '?') {
        return {Status::Ok, true, at + 1};
    }
    if (!atom.quoted && atom.value == '[') {
        return bracket(pattern, at, value);
    }
    return {Status::Ok, atom.value == value, at + 1};
}

[[nodiscard]] Status validate_brackets(
    std::span<const PatternByte> pattern) {
    for (std::size_t at = 0; at < pattern.size(); ++at) {
        if (pattern[at].quoted || pattern[at].value != '[') continue;
        const auto result = bracket(pattern, at, '\0');
        if (result.status != Status::Ok) return result.status;
        at = result.next - 1;
    }
    return Status::Ok;
}

}  // namespace

PatternResult match_case_pattern(std::span<const PatternByte> pattern,
                                  std::string_view value) {
    const auto valid = validate_brackets(pattern);
    if (valid != Status::Ok) return {valid, false};
    constexpr auto absent = static_cast<std::size_t>(-1);
    std::size_t at = 0;
    std::size_t text_at = 0;
    std::size_t star = absent;
    std::size_t star_text = 0;
    while (text_at < value.size()) {
        if (at < pattern.size() && !pattern[at].quoted &&
            pattern[at].value == '*') {
            star = at++;
            star_text = text_at;
            continue;
        }
        if (at < pattern.size()) {
            const auto atom = match_atom(pattern, at, value[text_at]);
            if (atom.status != Status::Ok) return {atom.status, false};
            if (atom.matched) {
                at = atom.next;
                ++text_at;
                continue;
            }
        }
        if (star == absent) return {Status::Ok, false};
        at = star + 1;
        text_at = ++star_text;
    }
    while (at < pattern.size() && !pattern[at].quoted &&
           pattern[at].value == '*') ++at;
    return {Status::Ok, at == pattern.size()};
}

}  // namespace mm::shell
