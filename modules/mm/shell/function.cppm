// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:function;

import :source;
import :status;
import :syntax;

export namespace mm::shell {

struct FunctionSlot {
    std::string_view name;
    EmbeddedScript body;
};

struct FunctionStorage {
    std::span<FunctionSlot> functions;
    std::span<char> text;
    std::span<ScriptToken> tokens;
    std::span<WordFragment> fragments;
    std::span<SyntaxNode> nodes;
    std::span<SyntaxLink> links;
    std::span<ParserFrame> parser_context;
};

struct FunctionResult {
    Status status = Status::Ok;
    OverflowInfo overflow;

    [[nodiscard]] constexpr bool ok() const {
        return status == Status::Ok;
    }
};

// All spans are caller-owned and non-aliasing. Definitions retain no view
// into name, source, or parser scratch after define returns.
class FunctionLibrary {
public:
    explicit FunctionLibrary(FunctionStorage storage);

    [[nodiscard]] FunctionResult define(std::string_view name,
                                        SourceView body);
    [[nodiscard]] const FunctionSlot* find(
        std::string_view name) const;
    // Copies another library's visible handles without copying its arena, so a
    // command-substitution child starts from the parent's definitions and
    // defines new ones in its own storage. The source arena must outlive every
    // adopted handle. Existing definitions here are discarded.
    [[nodiscard]] FunctionResult adopt(const FunctionLibrary& source);
    [[nodiscard]] std::size_t size() const { return count_; }
    void reset();

private:
    FunctionStorage storage_;
    std::size_t count_ = 0;
    std::size_t text_used_ = 0;
    std::size_t tokens_used_ = 0;
    std::size_t fragments_used_ = 0;
    std::size_t nodes_used_ = 0;
    std::size_t links_used_ = 0;
};

}  // namespace mm::shell
