// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :pathname;
import :service;
import mm.shell;

namespace mm::shell::full {
namespace {

[[nodiscard]] bool pattern_byte(char c) {
    return c == '*' || c == '?' || c == '[';
}

[[nodiscard]] bool matches(std::string_view pattern, std::string_view name) {
    std::vector<PatternByte> bytes;
    bytes.reserve(pattern.size());
    for (const char c : pattern) bytes.push_back({c, false});
    const auto result = match_case_pattern(bytes, name);
    return result.status == Status::Ok && result.matched;
}

// A leading period is matched only by a literal one, so a bare * never
// selects a dot file.
[[nodiscard]] bool hidden_is_admitted(std::string_view pattern,
                                      std::string_view name) {
    if (name.empty() || name.front() != '.') return true;
    return !pattern.empty() && pattern.front() == '.';
}

struct Candidate {
    // The path as the word spelled it, which is what the shell substitutes.
    std::string spelling;
    // The path the service is asked about, which carries the working
    // directory for a relative word.
    std::string request;
};

void split(std::string_view word, std::vector<std::string>& out,
           bool& absolute) {
    absolute = !word.empty() && word.front() == '/';
    std::size_t at = 0;
    while (at < word.size()) {
        while (at < word.size() && word[at] == '/') ++at;
        const auto start = at;
        while (at < word.size() && word[at] != '/') ++at;
        if (at > start) out.emplace_back(word.substr(start, at - start));
    }
}

[[nodiscard]] std::string join(std::string_view left,
                               std::string_view right) {
    if (left.empty()) return std::string{right};
    std::string joined{left};
    if (joined.back() != '/') joined.push_back('/');
    joined.append(right);
    return joined;
}

}  // namespace

bool has_pattern(std::string_view word) {
    for (const char c : word) {
        if (pattern_byte(c)) return true;
    }
    return false;
}

PathnameResult expand_pathname(std::string_view word,
                               std::string_view directory,
                               FileService files) {
    PathnameResult result;
    if (!has_pattern(word)) return result;
    if (files.list == nullptr) {
        result.service = ServiceStatus::Invalid;
        return result;
    }

    std::vector<std::string> components;
    bool absolute = false;
    split(word, components, absolute);
    if (components.empty()) return result;

    // The frontier holds every directory still consistent with the components
    // consumed so far. A component with no pattern byte is appended without a
    // listing, so a literal path costs no service call.
    std::vector<Candidate> frontier;
    frontier.push_back({absolute ? std::string{"/"} : std::string{},
                        absolute ? std::string{"/"}
                                 : std::string{directory}});
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto& component = components[index];
        std::vector<Candidate> next;
        if (!has_pattern(component)) {
            for (const auto& candidate : frontier) {
                next.push_back({join(candidate.spelling, component),
                                join(candidate.request, component)});
            }
            frontier = next;
            continue;
        }
        for (const auto& candidate : frontier) {
            std::vector<std::string> names;
            const auto listed = files.list(files.context, candidate.request,
                                           names);
            if (listed == ServiceStatus::NotFound ||
                listed == ServiceStatus::PermissionDenied) {
                // An unreadable directory contributes no match, exactly as a
                // shell that cannot open it contributes none.
                continue;
            }
            if (listed != ServiceStatus::Ok) {
                result.service = listed;
                return result;
            }
            std::sort(names.begin(), names.end());
            for (const auto& name : names) {
                if (!hidden_is_admitted(component, name)) continue;
                if (!matches(component, name)) continue;
                next.push_back({join(candidate.spelling, name),
                                join(candidate.request, name)});
            }
        }
        frontier = next;
        if (frontier.empty()) return result;
    }

    // Only a word whose last component was itself listed is a match; a
    // trailing literal component is verified when the service can report it.
    const auto verify = files.status != nullptr &&
                        !has_pattern(components.back());
    for (const auto& candidate : frontier) {
        if (verify) {
            bool exists = false;
            bool is_directory = false;
            const auto reported = files.status(files.context,
                                               candidate.request, exists,
                                               is_directory);
            if (reported != ServiceStatus::Ok) {
                result.service = reported;
                return result;
            }
            if (!exists) continue;
        }
        result.matches.push_back(candidate.spelling);
    }
    std::sort(result.matches.begin(), result.matches.end());
    result.expanded = !result.matches.empty();
    return result;
}

}  // namespace mm::shell::full
