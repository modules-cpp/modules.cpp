// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.mcu:i2s;

import :status;
import :i2s_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline Status i2s_configure(const I2sConfiguration& configuration) {
    return platform().i2s_configure(configuration);
}

[[nodiscard]] inline Status i2s_write(unsigned int instance,
                                      std::span<const std::byte> data) {
    return platform().i2s_write(instance, data);
}

[[nodiscard]] inline Status i2s_read(unsigned int instance, std::span<std::byte> data) {
    return platform().i2s_read(instance, data);
}

}
