// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.mcu:spi;

import :status;
import :spi_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline Status spi_configure(const SpiConfiguration& configuration) {
    return platform().spi_configure(configuration);
}

[[nodiscard]] inline Status spi_write(unsigned int instance,
                                      std::span<const std::byte> data) {
    return platform().spi_write(instance, data);
}

[[nodiscard]] inline Status spi_transfer(unsigned int instance,
                                         std::span<const std::byte> transmit,
                                         std::span<std::byte> receive) {
    return platform().spi_transfer(instance, transmit, receive);
}

}
