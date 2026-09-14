// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.display:platform;

import :status;
import :types;

export namespace mm::display {

class Display {
public:
    virtual ~Display() = default;

    [[nodiscard]] virtual Geometry geometry() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }
    [[nodiscard]] virtual Status clear(Color) { return Status::Unsupported; }
    [[nodiscard]] virtual Status write(Rectangle, std::span<const std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status refresh(Refresh) { return Status::Unsupported; }
    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};

void set_display(Display& display);
[[nodiscard]] Display& selected_display();

}
