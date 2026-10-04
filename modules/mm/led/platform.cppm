// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <span>

export module mm.led:platform;

import :status;
import :types;

export namespace mm::led {

class Led {
public:
    virtual ~Led() = default;

    [[nodiscard]] virtual unsigned int count() const { return 0; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }
    [[nodiscard]] virtual Status write(unsigned int, std::span<const Color>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status refresh() { return Status::Unsupported; }
    [[nodiscard]] virtual Status clear() { return Status::Unsupported; }
    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};

void set_led(Led& led);
[[nodiscard]] Led& selected_led();

}
