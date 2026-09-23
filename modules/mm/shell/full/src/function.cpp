// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module mm.shell.full;

import :function;
import :parse;
import :syntax;
import mm.shell;

namespace mm::shell::full {
namespace {

[[nodiscard]] bool first_byte(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_byte(char c) {
    return first_byte(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool reserved(std::string_view name) {
    constexpr std::string_view words[]{
        "if", "then", "else", "elif", "fi", "for", "in", "do", "done",
        "while", "case", "esac", "break", "continue", "exit", "return",
        "set", "shift", "until",
    };
    for (const auto word : words) {
        if (name == word) return true;
    }
    return false;
}

[[nodiscard]] bool valid_name(std::string_view name) {
    if (name.empty() || !first_byte(name.front())) return false;
    for (const char c : name.substr(1)) {
        if (!name_byte(c)) return false;
    }
    return !reserved(name);
}

}  // namespace

DefineResult FullFunctionLibrary::define(std::string_view name,
                                         std::string_view body) {
    if (!valid_name(name)) return {Status::BadArgument, {}};
    // The body is parsed into its own owning script first. Only a complete
    // parse reaches the table, so a failed redefinition is invisible.
    auto parsed = parse_full(body);
    if (!parsed.ok()) {
        return {parsed.diagnostic.status == ParseStatus::Unsupported
                    ? Status::Unsupported
                    : Status::BadArgument,
                parsed.diagnostic};
    }
    auto owned = std::make_shared<const FullScript>(
        std::move(parsed.script));
    for (auto& function : functions_) {
        if (function.name != name) continue;
        // Replacing the handle leaves any running body holding the old one.
        function.body = std::move(owned);
        return {};
    }
    functions_.push_back({std::string{name}, std::move(owned)});
    return {};
}

const FullFunction* FullFunctionLibrary::find(std::string_view name) const {
    for (const auto& function : functions_) {
        if (function.name == name) return &function;
    }
    return nullptr;
}

std::shared_ptr<const FullScript> FullFunctionLibrary::body(
    std::string_view name) const {
    const auto* function = find(name);
    return function == nullptr ? nullptr : function->body;
}

bool FullFunctionLibrary::remove(std::string_view name) {
    for (std::size_t i = 0; i < functions_.size(); ++i) {
        if (functions_[i].name != name) continue;
        functions_.erase(functions_.begin() +
                         static_cast<std::ptrdiff_t>(i));
        return true;
    }
    return false;
}

}  // namespace mm::shell::full
