// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.shell:source;

export namespace mm::shell {

struct SourceLocation {
    std::size_t offset = 0;
    unsigned int line = 1;
    unsigned int column = 1;
};

struct SourceSpan {
    std::size_t offset = 0;
    std::size_t length = 0;
};

class SourceView {
public:
    constexpr SourceView() = default;
    constexpr explicit SourceView(std::string_view text) : text_(text) {}

    [[nodiscard]] constexpr std::string_view text() const { return text_; }
    [[nodiscard]] constexpr std::size_t size() const { return text_.size(); }
    [[nodiscard]] constexpr bool empty() const { return text_.empty(); }

    [[nodiscard]] constexpr std::string_view slice(
        std::size_t offset, std::size_t count) const {
        if (offset >= text_.size()) return {};
        return text_.substr(offset, count);
    }

    [[nodiscard]] constexpr std::string_view slice(SourceSpan span) const {
        return slice(span.offset, span.length);
    }

private:
    std::string_view text_;
};

}  // namespace mm::shell
