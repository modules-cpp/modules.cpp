// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <limits>
#include <span>
#include <string_view>

module mm.shell;

import :function;
import :parse;
import :source;
import :status;
import :syntax;

namespace mm::shell {
namespace {

[[nodiscard]] bool first(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool rest(char c) {
    return first(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool valid_name(std::string_view name) {
    if (name.empty() || !first(name.front())) return false;
    for (const char c : name.substr(1)) {
        if (!rest(c)) return false;
    }
    return name != "if" && name != "then" && name != "else" &&
           name != "elif" && name != "fi" && name != "for" &&
           name != "in" && name != "do" && name != "done" &&
           name != "while" && name != "case" && name != "esac" &&
           name != "break" && name != "continue" &&
           name != "exit" && name != "return" &&
           name != "set" && name != "shift";
}

[[nodiscard]] bool fits(std::size_t used, std::size_t required,
                        std::size_t capacity) {
    return used <= capacity && required <= capacity - used;
}

[[nodiscard]] std::size_t required_total(std::size_t used,
                                          std::size_t first,
                                          std::size_t second = 0) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (first > maximum - used) return maximum;
    const auto partial = used + first;
    if (second > maximum - partial) return maximum;
    return partial + second;
}

void copy(std::span<char> target, std::size_t at,
          std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        target[at + i] = text[i];
    }
}

}  // namespace

FunctionLibrary::FunctionLibrary(FunctionStorage storage)
    : storage_(storage) {}

const FunctionSlot* FunctionLibrary::find(
    std::string_view name) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (storage_.functions[i].name == name) {
            return &storage_.functions[i];
        }
    }
    return nullptr;
}

FunctionResult FunctionLibrary::define(std::string_view name,
                                       SourceView body) {
    if (!valid_name(name)) return {Status::BadArgument};
    std::size_t index = count_;
    for (std::size_t i = 0; i < count_; ++i) {
        if (storage_.functions[i].name == name) {
            index = i;
            break;
        }
    }
    if (index == count_ && count_ == storage_.functions.size()) {
        return {Status::Overflow,
                {StorageClass::Functions, count_ + 1}};
    }
    const auto measured = measure_embedded(body);
    if (measured.status != ParseStatus::Complete) {
        return {Status::BadArgument};
    }
    const auto& needed = measured.required;
    if (!fits(text_used_, name.size(), storage_.text.size()) ||
        !fits(text_used_ + name.size(), body.size(),
              storage_.text.size())) {
        return {Status::Overflow,
                {StorageClass::FunctionArena,
                 required_total(text_used_, name.size(),
                                body.size())}};
    }
    if (!fits(tokens_used_, needed.tokens, storage_.tokens.size())) {
        return {Status::Overflow,
                {StorageClass::FunctionArena,
                 required_total(tokens_used_, needed.tokens)}};
    }
    if (!fits(fragments_used_, needed.fragments,
              storage_.fragments.size())) {
        return {Status::Overflow,
                {StorageClass::FunctionArena,
                 required_total(fragments_used_,
                                needed.fragments)}};
    }
    if (!fits(nodes_used_, needed.nodes, storage_.nodes.size())) {
        return {Status::Overflow,
                {StorageClass::FunctionArena,
                 required_total(nodes_used_, needed.nodes)}};
    }
    if (!fits(links_used_, needed.links, storage_.links.size())) {
        return {Status::Overflow,
                {StorageClass::FunctionArena,
                 required_total(links_used_, needed.links)}};
    }
    if (storage_.parser_context.size() < needed.context) {
        return {Status::Overflow,
                {StorageClass::ParserContext, needed.context}};
    }

    const auto body_at = text_used_ + name.size();
    copy(storage_.text, text_used_, name);
    copy(storage_.text, body_at, body.text());
    const auto copied_source = SourceView{std::string_view{
        storage_.text.data() + body_at, body.size()}};
    ScriptStorage parser_storage{
        storage_.tokens.subspan(tokens_used_, needed.tokens),
        storage_.fragments.subspan(fragments_used_, needed.fragments),
        storage_.nodes.subspan(nodes_used_, needed.nodes),
        storage_.links.subspan(links_used_, needed.links),
        storage_.parser_context};
    EmbeddedScript parsed;
    const auto built = parse_embedded(copied_source, parser_storage,
                                      parsed);
    if (built.status != ParseStatus::Complete) {
        return {Status::BadArgument};
    }
    storage_.functions[index] = {
        {storage_.text.data() + text_used_, name.size()}, parsed};
    text_used_ = body_at + body.size();
    tokens_used_ += needed.tokens;
    fragments_used_ += needed.fragments;
    nodes_used_ += needed.nodes;
    links_used_ += needed.links;
    if (index == count_) ++count_;
    return {};
}

void FunctionLibrary::reset() {
    count_ = 0;
    text_used_ = 0;
    tokens_used_ = 0;
    fragments_used_ = 0;
    nodes_used_ = 0;
    links_used_ = 0;
}

}  // namespace mm::shell
