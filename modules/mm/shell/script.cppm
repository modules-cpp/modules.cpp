// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:script;

import :capability;
import :command;
import :source;
import :status;
import :syntax;

export namespace mm::shell {

// The script equivalent of CommandDescriptor. Unlike a function definition,
// installed source is borrowed rather than copied, so name, summary, and
// source must outlive the library.
struct ScriptDescriptor {
    std::string_view name;
    std::string_view summary;
    CapabilitySet required_capabilities;
    SourceView source;
};

struct ScriptSlot {
    ScriptDescriptor descriptor;
    EmbeddedScript script;
};

struct ScriptLibraryStorage {
    std::span<ScriptSlot> scripts;
    std::span<ScriptToken> tokens;
    std::span<WordFragment> fragments;
    std::span<SyntaxNode> nodes;
    std::span<SyntaxLink> links;
    std::span<ParserFrame> parser_context;
};

struct ScriptResult {
    Status status = Status::Ok;
    OverflowInfo overflow;

    [[nodiscard]] constexpr bool ok() const {
        return status == Status::Ok;
    }
};

// ScriptLibrary owns caller-supplied slots and a monotonic parse arena. An
// install measures, preflights every storage class, parses, and commits one
// descriptor and script together. Any failure leaves the library and every
// arena cursor exactly as they were. There is no replacement or removal;
// reset reclaims the whole arena.
//
// An absent runtime capability does not prevent installation. Invocation
// reports Unavailable before evaluating the script's first command.
class ScriptLibrary {
public:
    explicit ScriptLibrary(ScriptLibraryStorage storage);

    // A native command and an installed script may not share a name, so pass
    // the registry that will resolve alongside this library.
    [[nodiscard]] ScriptResult install(const ScriptDescriptor& descriptor,
                                       const Registry* natives = nullptr);
    // All or none: every descriptor is validated and measured before the
    // first one is committed.
    [[nodiscard]] ScriptResult install_pack(
        std::span<const ScriptDescriptor> pack,
        const Registry* natives = nullptr);

    [[nodiscard]] const ScriptSlot* find(std::string_view name) const;
    [[nodiscard]] std::size_t size() const { return used_.count; }
    [[nodiscard]] std::span<const ScriptSlot> scripts() const {
        return storage_.scripts.subspan(0, used_.count);
    }
    void reset();

private:
    struct Cursors {
        std::size_t count = 0;
        std::size_t tokens = 0;
        std::size_t fragments = 0;
        std::size_t nodes = 0;
        std::size_t links = 0;
    };

    [[nodiscard]] ScriptResult admit(const ScriptDescriptor& descriptor,
                                     const Registry* natives) const;
    [[nodiscard]] ScriptResult commit(const ScriptDescriptor& descriptor);

    ScriptLibraryStorage storage_;
    Cursors used_;
};

}  // namespace mm::shell
