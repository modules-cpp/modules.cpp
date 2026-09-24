// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :expand;
import :pathname;
import :service;
import :word;
import mm.shell;

namespace mm::shell::full {
namespace {

constexpr char unquoted_byte = '0';
constexpr char quoted_byte = '1';

[[nodiscard]] bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] bool name_rest(char c) {
    return name_start(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool digit(char c) { return c >= '0' && c <= '9'; }

[[nodiscard]] bool pattern_metacharacter(char c) {
    return c == '*' || c == '?' || c == '[';
}

// Matches the closing byte of a paired form, counting nesting.
[[nodiscard]] std::size_t closing(std::string_view text, std::size_t open,
                                  char opener, char closer) {
    std::size_t depth = 0;
    for (std::size_t at = open; at < text.size(); ++at) {
        if (text[at] == opener) {
            ++depth;
            continue;
        }
        if (text[at] != closer) continue;
        --depth;
        if (depth == 0) return at;
    }
    return std::string_view::npos;
}

// The full-profile ${...} operators. mm.shell parses the three colon forms;
// the four pattern trims and ${#name} are added here.
enum class Operator {
    None, Default, Alternate, Assign, Error,
    TrimShortPrefix, TrimLongPrefix, TrimShortSuffix, TrimLongSuffix,
    Length,
};

struct Reference {
    bool valid = false;
    std::string_view name;
    Operator operation = Operator::None;
    std::string_view operand;
    // True for a colon form, which treats an empty value as unset.
    bool null_counts_as_unset = false;
};

[[nodiscard]] Reference parse_reference(std::string_view body) {
    Reference reference;
    if (body.empty()) return reference;
    if (body.front() == '#') {
        if (body.size() == 1) {
            // ${#} is the argument count, not a length request.
            reference.valid = true;
            reference.name = body;
            return reference;
        }
        reference.valid = true;
        reference.operation = Operator::Length;
        reference.name = body.substr(1);
        return reference;
    }
    std::size_t at = 0;
    while (at < body.size() && body[at] != ':' && body[at] != '#' &&
           body[at] != '%' && body[at] != '-' && body[at] != '+' &&
           body[at] != '=' && body[at] != '?') {
        ++at;
    }
    reference.name = body.substr(0, at);
    if (reference.name.empty()) return reference;
    if (at == body.size()) {
        reference.valid = true;
        return reference;
    }
    auto rest = body.substr(at);
    if (rest.front() == ':') {
        reference.null_counts_as_unset = true;
        rest.remove_prefix(1);
        if (rest.empty()) return reference;
    }
    const auto mark = rest.front();
    if (mark == '#' || mark == '%') {
        // A trim has no colon form.
        if (reference.null_counts_as_unset) return reference;
        const bool longest = rest.size() > 1 && rest[1] == mark;
        reference.operation = mark == '#'
            ? (longest ? Operator::TrimLongPrefix
                       : Operator::TrimShortPrefix)
            : (longest ? Operator::TrimLongSuffix
                       : Operator::TrimShortSuffix);
        reference.operand = rest.substr(longest ? 2 : 1);
        reference.valid = true;
        return reference;
    }
    switch (mark) {
        case '-': reference.operation = Operator::Default; break;
        case '+': reference.operation = Operator::Alternate; break;
        case '=': reference.operation = Operator::Assign; break;
        case '?': reference.operation = Operator::Error; break;
        default: return reference;
    }
    reference.operand = rest.substr(1);
    reference.valid = true;
    return reference;
}

// Accumulates expanded bytes with a per-byte quoting mask, plus the offsets
// where a quoted "$@" argument ends. The mask is what lets field splitting and
// pathname expansion tell a literal metacharacter from an expanded one.
class Builder {
public:
    void literal(std::string_view text, bool quoted) {
        // A quoted piece counts even when it is empty: "" and "$unset" are
        // each one empty field, while an unquoted empty expansion is none.
        if (quoted) quoted_ = true;
        for (const char c : text) {
            text_.push_back(c);
            mask_.push_back(quoted ? quoted_byte : unquoted_byte);
        }
    }

    void boundary() { boundaries_.push_back(text_.size()); }

    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] const std::string& mask() const { return mask_; }
    [[nodiscard]] const std::vector<std::size_t>& boundaries() const {
        return boundaries_;
    }
    void mark_produced() { produced_ = true; }
    // Entering a quoted section counts even if it turns out to be empty.
    void mark_quoted() {
        quoted_ = true;
        produced_ = true;
    }
    [[nodiscard]] bool produced() const { return produced_; }
    // True when a quoted piece was emitted, which forces one field even if
    // every piece was empty.
    [[nodiscard]] bool quoted() const { return quoted_; }

private:
    std::string text_;
    std::string mask_;
    std::vector<std::size_t> boundaries_;
    bool produced_ = false;
    bool quoted_ = false;
};

struct Field {
    std::string text;
    std::string mask;
};

[[nodiscard]] bool is_ifs(char c, std::string_view ifs) {
    return ifs.find(c) != std::string_view::npos;
}

// Splits on unquoted IFS bytes, and additionally at each recorded boundary so
// that "$@" stays one field per argument even when IFS does not separate them.
[[nodiscard]] std::vector<Field> split_into_fields(
    const Builder& builder, std::string_view ifs, bool split) {
    std::vector<Field> fields;
    Field current;
    auto flush = [&fields, &current](bool always) {
        if (always || !current.text.empty()) {
            fields.push_back(current);
        }
        current = {};
    };
    std::size_t boundary_index = 0;
    for (std::size_t at = 0; at < builder.text().size(); ++at) {
        while (boundary_index < builder.boundaries().size() &&
               builder.boundaries()[boundary_index] == at) {
            flush(true);
            ++boundary_index;
        }
        const auto byte = builder.text()[at];
        const auto quoted = builder.mask()[at] == quoted_byte;
        if (split && !quoted && is_ifs(byte, ifs)) {
            flush(false);
            continue;
        }
        current.text.push_back(byte);
        current.mask.push_back(builder.mask()[at]);
    }
    while (boundary_index < builder.boundaries().size()) {
        flush(true);
        ++boundary_index;
    }
    if (!current.text.empty()) {
        fields.push_back(current);
    } else if (fields.empty() && (builder.quoted() || !builder.produced())) {
        fields.push_back(current);
    }
    return fields;
}

[[nodiscard]] bool globbable(const Field& field) {
    bool unquoted_pattern = false;
    bool quoted_pattern = false;
    for (std::size_t i = 0; i < field.text.size(); ++i) {
        if (!pattern_metacharacter(field.text[i])) continue;
        if (field.mask[i] == quoted_byte) {
            quoted_pattern = true;
        } else {
            unquoted_pattern = true;
        }
    }
    return unquoted_pattern && !quoted_pattern;
}

class Expander {
public:
    Expander(ShellState& state, ExpandRequest request, FileService files,
             Substituter substituter)
        : state_(state), request_(request), files_(files),
          substituter_(substituter) {}

    [[nodiscard]] WordFields run(std::string_view spelling) {
        WordFields result;
        // An isolated quoted $@ contributes no field when there are no
        // positionals. Ordinary empty quotes still contribute one.
        if (state_.argument_count() == 0 &&
            (spelling == "\"$@\"" || spelling == "\"${@}\"")) {
            return result;
        }
        if (!scan(spelling, result)) return result;
        auto fields = split_into_fields(builder_, state_.ifs(),
                                       request_.split);
        for (const auto& field : fields) {
            if (!request_.pathname || !globbable(field)) {
                result.fields.push_back(field.text);
                continue;
            }
            const auto matched = expand_pathname(field.text,
                                                request_.directory, files_);
            if (!matched.ok()) {
                result.service = matched.service;
                return result;
            }
            if (!matched.expanded) {
                result.fields.push_back(field.text);
                continue;
            }
            for (const auto& match : matched.matches) {
                result.fields.push_back(match);
            }
        }
        result.substitution_status = substitution_status_;
        return result;
    }

private:
    [[nodiscard]] bool fail(WordFields& result, Status status,
                            std::size_t at) {
        result.status = status;
        result.issue = at;
        return false;
    }

    [[nodiscard]] bool scan(std::string_view text, WordFields& result) {
        for (std::size_t at = 0; at < text.size();) {
            const auto byte = text[at];
            if (byte == '\'') {
                const auto close = text.find('\'', at + 1);
                if (close == std::string_view::npos) {
                    return fail(result, Status::BadArgument, at);
                }
                builder_.mark_quoted();
                builder_.literal(text.substr(at + 1, close - at - 1), true);
                at = close + 1;
                continue;
            }
            if (byte == '"') {
                builder_.mark_quoted();
                const auto consumed = double_quoted(text, at + 1, result);
                if (consumed == std::string_view::npos) return false;
                at = consumed;
                continue;
            }
            if (byte == '\\') {
                if (at + 1 == text.size()) {
                    return fail(result, Status::BadArgument, at);
                }
                if (text[at + 1] != '\n') {
                    builder_.literal(text.substr(at + 1, 1), true);
                }
                builder_.mark_produced();
                at += 2;
                continue;
            }
            if (byte == '`') return fail(result, Status::Unsupported, at);
            if (byte == '$') {
                const auto consumed = dollar(text, at, false, result);
                if (consumed == std::string_view::npos) return false;
                at = consumed;
                continue;
            }
            builder_.literal(text.substr(at, 1), false);
            ++at;
        }
        return true;
    }

    // Returns the offset just past the closing quote, or npos on failure.
    [[nodiscard]] std::size_t double_quoted(std::string_view text,
                                            std::size_t at,
                                            WordFields& result) {
        while (at < text.size() && text[at] != '"') {
            const auto byte = text[at];
            if (byte == '\\' && at + 1 < text.size()) {
                const auto next = text[at + 1];
                // Inside double quotes a backslash escapes only these.
                if (next == '$' || next == '`' || next == '"' ||
                    next == '\\') {
                    builder_.literal(text.substr(at + 1, 1), true);
                    at += 2;
                    continue;
                }
                if (next == '\n') {
                    at += 2;
                    continue;
                }
                builder_.literal(text.substr(at, 1), true);
                ++at;
                continue;
            }
            if (byte == '`') {
                (void)fail(result, Status::Unsupported, at);
                return std::string_view::npos;
            }
            if (byte == '$') {
                const auto consumed = dollar(text, at, true, result);
                if (consumed == std::string_view::npos) {
                    return std::string_view::npos;
                }
                at = consumed;
                continue;
            }
            builder_.literal(text.substr(at, 1), true);
            ++at;
        }
        if (at == text.size()) {
            (void)fail(result, Status::BadArgument, at);
            return std::string_view::npos;
        }
        return at + 1;
    }

    // Returns the offset just past the expansion, or npos on failure.
    [[nodiscard]] std::size_t dollar(std::string_view text, std::size_t at,
                                     bool quoted, WordFields& result) {
        if (at + 2 < text.size() && text[at + 1] == '(' &&
            text[at + 2] == '(') {
            const auto inner = closing(text, at + 2, '(', ')');
            if (inner == std::string_view::npos ||
                inner + 1 >= text.size() || text[inner + 1] != ')') {
                (void)fail(result, Status::BadArgument, at);
                return std::string_view::npos;
            }
            const auto value = evaluate_full_arithmetic(
                text.substr(at + 3, inner - (at + 3)), state_);
            if (!value.ok()) {
                (void)fail(result, Status::BadArgument, at);
                return std::string_view::npos;
            }
            builder_.literal(std::to_string(value.value), quoted);
            builder_.mark_produced();
            return inner + 2;
        }
        if (at + 1 < text.size() && text[at + 1] == '(') {
            const auto close = closing(text, at + 1, '(', ')');
            if (close == std::string_view::npos) {
                (void)fail(result, Status::BadArgument, at);
                return std::string_view::npos;
            }
            if (substituter_.run == nullptr) {
                (void)fail(result, Status::Unsupported, at);
                return std::string_view::npos;
            }
            std::string captured;
            int status = 0;
            const auto service = substituter_.run(
                substituter_.context,
                text.substr(at + 2, close - (at + 2)), captured, status);
            if (service != ServiceStatus::Ok) {
                result.service = service;
                result.issue = at;
                return std::string_view::npos;
            }
            substitution_status_ = status;
            builder_.literal(captured, quoted);
            builder_.mark_produced();
            return close + 1;
        }
        if (at + 1 < text.size() && text[at + 1] == '{') {
            const auto close = closing(text, at + 1, '{', '}');
            if (close == std::string_view::npos) {
                (void)fail(result, Status::BadArgument, at);
                return std::string_view::npos;
            }
            if (!braced(text.substr(at + 2, close - (at + 2)), quoted,
                        at, result)) {
                return std::string_view::npos;
            }
            return close + 1;
        }
        const auto length = bare_length(text, at + 1);
        if (length == 0) {
            builder_.literal(text.substr(at, 1), quoted);
            return at + 1;
        }
        if (!plain(text.substr(at + 1, length), quoted, at, result)) {
            return std::string_view::npos;
        }
        return at + 1 + length;
    }

    [[nodiscard]] static std::size_t bare_length(std::string_view text,
                                                 std::size_t at) {
        if (at >= text.size()) return 0;
        const auto c = text[at];
        if (c == '?' || c == '#' || c == '$' || c == '@' || c == '*' ||
            c == '!' || c == '-') {
            return 1;
        }
        if (digit(c)) return 1;
        if (!name_start(c)) return 0;
        std::size_t length = 0;
        while (at + length < text.size() && name_rest(text[at + length])) {
            ++length;
        }
        return length;
    }

    [[nodiscard]] bool plain(std::string_view name, bool quoted,
                             std::size_t at, WordFields& result) {
        if (name == "@" || name == "*") {
            return arguments(name == "@", quoted, at, result);
        }
        std::string value;
        auto found = false;
        if (!lookup(name, value, found)) {
            return fail(result, Status::Unsupported, at);
        }
        if (!found && state_.nounset) {
            return fail(result, Status::NotFound, at);
        }
        builder_.literal(value, quoted);
        builder_.mark_produced();
        return true;
    }

    [[nodiscard]] bool braced(std::string_view body, bool quoted,
                              std::size_t at, WordFields& result) {
        const auto reference = parse_reference(body);
        if (!reference.valid) return fail(result, Status::BadArgument, at);
        if (reference.name == "@" || reference.name == "*") {
            if (reference.operation != Operator::None) {
                return fail(result, Status::Unsupported, at);
            }
            return arguments(reference.name == "@", quoted, at, result);
        }
        std::string value;
        auto found = false;
        if (!lookup(reference.name, value, found)) {
            return fail(result, Status::Unsupported, at);
        }
        const auto empty = !found || value.empty();
        const auto unset = reference.null_counts_as_unset ? empty : !found;

        switch (reference.operation) {
            case Operator::Length:
                builder_.literal(std::to_string(value.size()), quoted);
                builder_.mark_produced();
                return true;
            case Operator::None:
                if (!found && state_.nounset) {
                    return fail(result, Status::NotFound, at);
                }
                builder_.literal(value, quoted);
                builder_.mark_produced();
                return true;
            case Operator::Default:
                if (!unset) {
                    builder_.literal(value, quoted);
                    builder_.mark_produced();
                    return true;
                }
                return nested(reference.operand, quoted, at, result);
            case Operator::Alternate:
                if (unset) {
                    builder_.mark_produced();
                    return true;
                }
                return nested(reference.operand, quoted, at, result);
            case Operator::Assign: {
                if (!unset) {
                    builder_.literal(value, quoted);
                    builder_.mark_produced();
                    return true;
                }
                std::string assigned;
                if (!nested_value(reference.operand, at, result, assigned)) {
                    return false;
                }
                if (!state_.assign(reference.name, assigned).ok()) {
                    return fail(result, Status::Overflow, at);
                }
                builder_.literal(assigned, quoted);
                builder_.mark_produced();
                return true;
            }
            case Operator::Error:
                if (!unset) {
                    builder_.literal(value, quoted);
                    builder_.mark_produced();
                    return true;
                }
                return fail(result, Status::NotFound, at);
            case Operator::TrimShortPrefix:
            case Operator::TrimLongPrefix:
            case Operator::TrimShortSuffix:
            case Operator::TrimLongSuffix: {
                std::string pattern;
                if (!nested_value(reference.operand, at, result, pattern)) {
                    return false;
                }
                const auto mode =
                    reference.operation == Operator::TrimShortPrefix
                        ? TrimMode::ShortestPrefix
                        : reference.operation == Operator::TrimLongPrefix
                              ? TrimMode::LongestPrefix
                              : reference.operation ==
                                        Operator::TrimShortSuffix
                                    ? TrimMode::ShortestSuffix
                                    : TrimMode::LongestSuffix;
                const auto trimmed = trim_parameter(value, pattern, mode);
                if (trimmed.status != Status::Ok) {
                    return fail(result, trimmed.status, at);
                }
                builder_.literal(trimmed.value, quoted);
                builder_.mark_produced();
                return true;
            }
        }
        return fail(result, Status::Unsupported, at);
    }

    // An operand is itself a word, so it expands with the same rules but never
    // splits or globs: its result is one value.
    [[nodiscard]] bool nested_value(std::string_view operand, std::size_t at,
                                    WordFields& result, std::string& out) {
        if (depth_ > 16) return fail(result, Status::Overflow, at);
        ++depth_;
        Expander inner{state_, {false, false, request_.directory}, files_,
                       substituter_};
        inner.depth_ = depth_;
        const auto expanded = inner.run(operand);
        --depth_;
        if (!expanded.ok()) {
            result.status = expanded.status;
            result.service = expanded.service;
            result.issue = at;
            return false;
        }
        out = expanded.fields.empty() ? std::string{} : expanded.fields[0];
        return true;
    }

    [[nodiscard]] bool nested(std::string_view operand, bool quoted,
                              std::size_t at, WordFields& result) {
        std::string value;
        if (!nested_value(operand, at, result, value)) return false;
        builder_.literal(value, quoted);
        builder_.mark_produced();
        return true;
    }

    [[nodiscard]] bool lookup(std::string_view name, std::string& out,
                              bool& found) {
        if (name == "#") {
            out = std::to_string(state_.argument_count());
            found = true;
            return true;
        }
        if (name == "?") {
            out = std::to_string(state_.last_status);
            found = true;
            return true;
        }
        if (name == "$") {
            out = std::to_string(state_.shell_id);
            found = true;
            return true;
        }
        if (name == "!" || name == "-") {
            // No job control and no option string at this level.
            return false;
        }
        if (!name.empty() && digit(name.front())) {
            std::size_t index = 0;
            for (const char c : name) {
                if (!digit(c)) return false;
                index = index * 10 + static_cast<std::size_t>(c - '0');
            }
            const auto value = state_.positional(index);
            found = value.found;
            out = std::string{value.value};
            return true;
        }
        const auto value = state_.lookup(name);
        found = value.found;
        out = std::string{value.value};
        return true;
    }

    [[nodiscard]] bool arguments(bool at_form, bool quoted, std::size_t at,
                                 WordFields& result) {
        (void)at;
        (void)result;
        const auto count = state_.argument_count();
        builder_.mark_produced();
        if (quoted && at_form) {
            // "$@" is one field per argument, whatever IFS says.
            for (std::size_t i = 1; i <= count; ++i) {
                if (i != 1) builder_.boundary();
                builder_.literal(state_.positional(i).value, true);
            }
            return true;
        }
        const auto ifs = state_.ifs();
        const auto separator = quoted
            ? (ifs.empty() ? std::string_view{} : ifs.substr(0, 1))
            : std::string_view{" "};
        for (std::size_t i = 1; i <= count; ++i) {
            if (i != 1) builder_.literal(separator, quoted);
            builder_.literal(state_.positional(i).value, quoted);
        }
        return true;
    }

    ShellState& state_;
    ExpandRequest request_;
    FileService files_;
    Substituter substituter_;
    Builder builder_;
    int substitution_status_ = 0;
    unsigned int depth_ = 0;
};

}  // namespace

WordFields expand_word_full(std::string_view spelling, ShellState& state,
                            ExpandRequest request, FileService files,
                            Substituter substituter) {
    Expander expander{state, request, files, substituter};
    return expander.run(spelling);
}

}  // namespace mm::shell::full
