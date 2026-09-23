// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
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
    const auto name_bytes = fresh ? name.size() : 0;
    if (name_bytes > variable_text_.size() - variable_text_used_) {
        return {Status::Overflow,
                {StorageClass::VariableText,
                 variable_text_used_ + name_bytes}};
    }
    const auto available = variable_text_.size() - variable_text_used_;
    if (value.size() > available - name_bytes) {
        return {Status::Overflow,
                {StorageClass::VariableText,
                 variable_text_used_ + name_bytes + value.size()}};
    }
    const auto bytes = name_bytes + value.size();
    SourceSpan name_span = fresh ? SourceSpan{variable_text_used_, name.size()}
                                 : variables_[index].name;
    if (fresh) copy_text(variable_text_, variable_text_used_, name);
    const auto value_at = variable_text_used_ + (fresh ? name.size() : 0);
    copy_text(variable_text_, value_at, value);
    variables_[index] = {name_span, {value_at, value.size()}};
    variable_text_used_ += bytes;
    if (fresh) ++variable_count_;
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
