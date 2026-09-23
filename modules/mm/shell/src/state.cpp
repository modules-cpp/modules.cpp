// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

module mm.shell;

import :source;
import :state;
import :status;

namespace mm::shell {
namespace {

[[nodiscard]] bool name_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_rest(char c) {
    return name_first(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool valid_name(std::string_view name) {
    if (name.empty() || !name_first(name.front())) return false;
    for (const char c : name.substr(1)) {
        if (!name_rest(c)) return false;
    }
    return true;
}

void copy_text(std::span<char> destination, std::size_t offset,
               std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        destination[offset + i] = text[i];
    }
}

[[nodiscard]] std::string_view view_text(std::span<const char> storage,
                                         SourceSpan span) {
    if (span.length == 0) return {};
    return {storage.data() + span.offset, span.length};
}

}  // namespace

ShellState::ShellState(std::span<VariableSlot> variables,
                       std::span<char> variable_text,
                       std::span<PositionalSlot> positionals,
                       std::span<char> positional_text)
    : variables_(variables), variable_text_(variable_text),
      positionals_(positionals), positional_text_(positional_text) {}

StateResult ShellState::assign(std::string_view name,
                                std::string_view value) {
    if (!valid_name(name)) return {Status::BadArgument, {}};
    std::size_t index = variable_count_;
    for (std::size_t i = 0; i < variable_count_; ++i) {
        if (view_text(variable_text_, variables_[i].name) == name) {
            index = i;
            break;
        }
    }
    const bool fresh = index == variable_count_;
    if (fresh && variable_count_ == variables_.size()) {
        return {Status::Overflow,
                {StorageClass::Variables, variable_count_ + 1}};
    }
    if (fresh) {
        const auto available = variable_text_.size() - variable_text_used_;
        if (name.size() > available ||
            value.size() > available - name.size()) {
            return {Status::Overflow,
                    {StorageClass::VariableText,
                     variable_text_used_ + name.size() + value.size()}};
        }
        const auto value_at = variable_text_used_ + name.size();
        copy_text(variable_text_, variable_text_used_, name);
        copy_text(variable_text_, value_at, value);
        variables_[index] = {{variable_text_used_, name.size()},
                             {value_at, value.size()}};
        variable_text_used_ = value_at + value.size();
        ++variable_count_;
        return {};
    }
    const auto old = variables_[index].value;
    const auto base = variable_text_used_ - old.length;
    if (value.size() > variable_text_.size() - base) {
        return {Status::Overflow,
                {StorageClass::VariableText, base + value.size()}};
    }
    const auto old_end = old.offset + old.length;
    const auto new_end = old.offset + value.size();
    const auto tail = variable_text_used_ - old_end;
    std::memmove(variable_text_.data() + new_end,
                 variable_text_.data() + old_end, tail);
    for (std::size_t i = 0; i < variable_count_; ++i) {
        if (variables_[i].name.offset >= old_end) {
            variables_[i].name.offset =
                variables_[i].name.offset - old.length + value.size();
        }
        if (i != index && variables_[i].value.offset >= old_end) {
            variables_[i].value.offset =
                variables_[i].value.offset - old.length + value.size();
        }
    }
    copy_text(variable_text_, old.offset, value);
    variables_[index].value.length = value.size();
    variable_text_used_ = base + value.size();
    return {};
}

ValueLookup ShellState::lookup(std::string_view name) const {
    for (std::size_t i = 0; i < variable_count_; ++i) {
        if (view_text(variable_text_, variables_[i].name) == name) {
            return {true, view_text(variable_text_, variables_[i].value)};
        }
    }
    return {};
}

std::string_view ShellState::ifs() const {
    const auto value = lookup("IFS");
    return value.found ? value.value : std::string_view{" \t\n"};
}

StateResult ShellState::set_positionals(
    std::string_view command_name,
    std::span<const std::string_view> arguments) {
    if (arguments.size() == static_cast<std::size_t>(-1)) {
        return {Status::Overflow, {StorageClass::PositionalParameters,
                                   arguments.size()}};
    }
    const auto required_slots = arguments.size() + 1;
    if (required_slots > positionals_.size()) {
        return {Status::Overflow,
                {StorageClass::PositionalParameters, required_slots}};
    }
    if (command_name.size() >
        positional_text_.size() - positional_text_used_) {
        return {Status::Overflow,
                {StorageClass::PositionalParameterText,
                 positional_text_used_ + command_name.size()}};
    }
    std::size_t required_bytes = positional_text_used_ +
                                 command_name.size();
    for (const auto argument : arguments) {
        if (argument.size() > positional_text_.size() - required_bytes) {
            return {Status::Overflow,
                    {StorageClass::PositionalParameterText,
                     required_bytes + argument.size()}};
        }
        required_bytes += argument.size();
    }
    if (required_bytes > positional_text_.size()) {
        return {Status::Overflow,
                {StorageClass::PositionalParameterText, required_bytes}};
    }
    const auto first = positional_text_used_;
    copy_text(positional_text_, first, command_name);
    positionals_[0] = {{first, command_name.size()}};
    auto offset = first + command_name.size();
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        copy_text(positional_text_, offset, arguments[i]);
        positionals_[i + 1] = {{offset, arguments[i].size()}};
        offset += arguments[i].size();
    }
    positional_count_ = required_slots;
    positional_text_used_ = offset;
    shifted_ = 0;
    return {};
}

ValueLookup ShellState::positional(std::size_t index) const {
    if (positional_count_ == 0) return {};
    const auto actual = index == 0 ? 0 : shifted_ + index;
    if (actual >= positional_count_) return {};
    return {true, view_text(positional_text_, positionals_[actual].value)};
}

std::size_t ShellState::argument_count() const {
    return positional_count_ == 0 ? 0 : positional_count_ - shifted_ - 1;
}

StateResult ShellState::shift(std::size_t count) {
    if (count > argument_count()) return {Status::BadArgument, {}};
    shifted_ += count;
    return {};
}

StateResult ShellState::push_positionals(
    std::string_view command_name,
    std::span<const std::string_view> arguments,
    std::span<PositionalSlot> saved, PositionalFrame& out) {
    if (saved.size() < positional_count_) {
        return {Status::Overflow,
                {StorageClass::PositionalParameters, positional_count_}};
    }
    // The slots are copied before the replacement overwrites them; the text
    // pool is monotonic, so the saved spans stay valid underneath the call.
    for (std::size_t i = 0; i < positional_count_; ++i) {
        saved[i] = positionals_[i];
    }
    const PositionalFrame frame{positional_count_, positional_text_used_,
                                shifted_};
    const auto installed = set_positionals(command_name, arguments);
    if (!installed.ok()) return installed;
    out = frame;
    return {};
}

StateResult ShellState::pop_positionals(
    std::span<const PositionalSlot> saved, const PositionalFrame& frame) {
    if (frame.count > positionals_.size() || frame.count > saved.size() ||
        frame.text_used > positional_text_.size()) {
        return {Status::BadArgument, {}};
    }
    for (std::size_t i = 0; i < frame.count; ++i) {
        positionals_[i] = saved[i];
    }
    positional_count_ = frame.count;
    positional_text_used_ = frame.text_used;
    shifted_ = frame.shifted;
    return {};
}

StateResult ShellState::fork_variables(
    std::span<VariableSlot> variables, std::span<char> variable_text,
    ShellState& out) const {
    if (variables.size() < variable_count_) {
        return {Status::Overflow,
                {StorageClass::Variables, variable_count_}};
    }
    if (variable_text.size() < variable_text_used_) {
        return {Status::Overflow,
                {StorageClass::VariableText, variable_text_used_}};
    }
    for (std::size_t i = 0; i < variable_count_; ++i) {
        variables[i] = variables_[i];
    }
    for (std::size_t i = 0; i < variable_text_used_; ++i) {
        variable_text[i] = variable_text_[i];
    }
    // A fork cannot grow beyond its parent even when scratch is larger.
    // This makes a successful shadow assignment safe to commit.
    const auto slots = variables.first(
        variables.size() < variables_.size() ? variables.size()
                                             : variables_.size());
    const auto bytes = variable_text.first(
        variable_text.size() < variable_text_.size()
            ? variable_text.size() : variable_text_.size());
    ShellState next{slots, bytes, positionals_, positional_text_};
    next.last_status = last_status;
    next.errexit = errexit;
    next.nounset = nounset;
    next.shell_id = shell_id;
    next.variable_count_ = variable_count_;
    next.variable_text_used_ = variable_text_used_;
    next.positional_count_ = positional_count_;
    next.positional_text_used_ = positional_text_used_;
    next.shifted_ = shifted_;
    out = next;
    return {};
}

StateResult ShellState::commit_variables_from(const ShellState& fork) {
    if (variables_.size() < fork.variable_count_) {
        return {Status::Overflow,
                {StorageClass::Variables, fork.variable_count_}};
    }
    if (variable_text_.size() < fork.variable_text_used_) {
        return {Status::Overflow,
                {StorageClass::VariableText, fork.variable_text_used_}};
    }
    for (std::size_t i = 0; i < fork.variable_count_; ++i) {
        variables_[i] = fork.variables_[i];
    }
    for (std::size_t i = 0; i < fork.variable_text_used_; ++i) {
        variable_text_[i] = fork.variable_text_[i];
    }
    variable_count_ = fork.variable_count_;
    variable_text_used_ = fork.variable_text_used_;
    return {};
}

void ShellState::reset() {
    last_status = 0;
    errexit = false;
    nounset = false;
    variable_count_ = 0;
    variable_text_used_ = 0;
    positional_count_ = 0;
    positional_text_used_ = 0;
    shifted_ = 0;
}

}  // namespace mm::shell
