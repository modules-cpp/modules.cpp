// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.stdio:platform;

import :status;

export namespace mm::stdio {

class Console {
public:
    virtual ~Console() = default;

    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // written may be less than the span, and is written only when the call
    // answers Ok. A console with nowhere to put bytes reports what it accepted
    // rather than blocking until it can or claiming a delivery it did not make.
    [[nodiscard]] virtual Status write(std::span<const std::byte>, std::size_t&) {
        return Status::Unsupported;
    }

    // count zero is Ok: nothing waiting is an answer, not a failure.
    [[nodiscard]] virtual Status read(std::span<std::byte>, std::size_t&) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status flush() { return Status::Unsupported; }

    // Whether anything is listening. A console that cannot tell answers
    // Unsupported rather than guessing true, because a caller that polls this
    // before writing would then wait forever on a guess.
    [[nodiscard]] virtual Status connected(bool&) { return Status::Unsupported; }
};

void set_console(Console& console);
[[nodiscard]] Console& selected_console();

}
