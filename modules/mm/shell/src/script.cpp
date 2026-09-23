// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :command;
import :parse;
import :script;
import :source;
import :status;
import :syntax;

namespace mm::shell {
namespace {

[[nodiscard]] bool first_byte(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool name_byte(char c) {
    return first_byte(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] bool valid_script_name(std::string_view name) {
    if (name.empty() || !first_byte(name.front())) return false;
    for (const char c : name.substr(1)) {
        if (!name_byte(c)) return false;
    }
    return true;
}

[[nodiscard]] bool fits(std::size_t used, std::size_t wanted,
                        std::size_t capacity) {
    return used <= capacity && wanted <= capacity - used;
}

}  // namespace

ScriptLibrary::ScriptLibrary(ScriptLibraryStorage storage)
    : storage_(storage) {}

// Validation and measurement only: nothing here touches the arena, so a pack
// can be admitted entirely before the first script is committed.
ScriptResult ScriptLibrary::admit(const ScriptDescriptor& descriptor,
                                 const Registry* natives) const {
    if (!valid_script_name(descriptor.name)) {
        return {Status::BadArgument};
    }
    if (find(descriptor.name) != nullptr) return {Status::Duplicate};
    if (natives != nullptr && natives->find(descriptor.name) != nullptr) {
        return {Status::Duplicate};
    }
    if (!fits(used_.count, 1, storage_.scripts.size())) {
        return {Status::Overflow, {StorageClass::Scripts, used_.count + 1}};
    }
    const auto measured = measure_embedded(descriptor.source);
    if (measured.status != ParseStatus::Complete) {
        return {Status::BadArgument};
    }
    const auto& needed = measured.required;
    if (!fits(used_.tokens, needed.tokens, storage_.tokens.size())) {
        return {Status::Overflow,
                {StorageClass::ScriptArena, used_.tokens + needed.tokens}};
    }
    if (!fits(used_.fragments, needed.fragments,
              storage_.fragments.size())) {
        return {Status::Overflow,
                {StorageClass::ScriptArena,
                 used_.fragments + needed.fragments}};
    }
    if (!fits(used_.nodes, needed.nodes, storage_.nodes.size())) {
        return {Status::Overflow,
                {StorageClass::ScriptArena, used_.nodes + needed.nodes}};
    }
    if (!fits(used_.links, needed.links, storage_.links.size())) {
        return {Status::Overflow,
                {StorageClass::ScriptArena, used_.links + needed.links}};
    }
    if (storage_.parser_context.size() < needed.context) {
        return {Status::Overflow,
                {StorageClass::ParserContext, needed.context}};
    }
    return {};
}

ScriptResult ScriptLibrary::commit(const ScriptDescriptor& descriptor) {
    const auto measured = measure_embedded(descriptor.source);
    const auto& needed = measured.required;
    ScriptStorage parser{
        storage_.tokens.subspan(used_.tokens, needed.tokens),
        storage_.fragments.subspan(used_.fragments, needed.fragments),
        storage_.nodes.subspan(used_.nodes, needed.nodes),
        storage_.links.subspan(used_.links, needed.links),
        storage_.parser_context};
    EmbeddedScript parsed;
    const auto built = parse_embedded(descriptor.source, parser, parsed);
    if (built.status != ParseStatus::Complete) {
        return {built.status == ParseStatus::Overflow ? Status::Overflow
                                                     : Status::BadArgument,
                built.overflow};
    }
    storage_.scripts[used_.count] = {descriptor, parsed};
    ++used_.count;
    used_.tokens += needed.tokens;
    used_.fragments += needed.fragments;
    used_.nodes += needed.nodes;
    used_.links += needed.links;
    return {};
}

ScriptResult ScriptLibrary::install(const ScriptDescriptor& descriptor,
                                    const Registry* natives) {
    const auto admitted = admit(descriptor, natives);
    if (!admitted.ok()) return admitted;
    const auto saved = used_;
    const auto committed = commit(descriptor);
    if (!committed.ok()) used_ = saved;
    return committed;
}

ScriptResult ScriptLibrary::install_pack(
    std::span<const ScriptDescriptor> pack, const Registry* natives) {
    const auto saved = used_;
    // Admission runs against the cursors the previous entry would leave, so
    // capacity is checked for the pack as a whole.
    for (std::size_t i = 0; i < pack.size(); ++i) {
        for (std::size_t j = i + 1; j < pack.size(); ++j) {
            if (pack[i].name == pack[j].name) {
                used_ = saved;
                return {Status::Duplicate};
            }
        }
        const auto admitted = admit(pack[i], natives);
        if (!admitted.ok()) {
            used_ = saved;
            return admitted;
        }
        const auto committed = commit(pack[i]);
        if (!committed.ok()) {
            used_ = saved;
            return committed;
        }
    }
    return {};
}

const ScriptSlot* ScriptLibrary::find(std::string_view name) const {
    for (std::size_t i = 0; i < used_.count; ++i) {
        if (storage_.scripts[i].descriptor.name == name) {
            return &storage_.scripts[i];
        }
    }
    return nullptr;
}

void ScriptLibrary::reset() { used_ = {}; }

}  // namespace mm::shell
