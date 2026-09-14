// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.touch:platform;

import :status;
import :types;

export namespace mm::touch {

class Touch {
public:
    virtual ~Touch() = default;

    [[nodiscard]] virtual Geometry geometry() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // count is written only when the call answers Ok, so a caller that ignores
    // the status cannot mistake a stale count for a fresh reading.
    [[nodiscard]] virtual Status read(std::span<Point>, std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};

void set_touch(Touch& touch);
[[nodiscard]] Touch& selected_touch();

}
