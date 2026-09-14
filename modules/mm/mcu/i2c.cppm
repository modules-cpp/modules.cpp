// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.mcu:i2c;

import :status;
import :i2c_types;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline Status i2c_configure(const I2cConfiguration& configuration) {
    return platform().i2c_configure(configuration);
}

[[nodiscard]] inline Status i2c_write(unsigned int instance, unsigned int address,
                                      std::span<const std::byte> data) {
    return platform().i2c_write(instance, address, data);
}

[[nodiscard]] inline Status i2c_read(unsigned int instance, unsigned int address,
                                     std::span<std::byte> data) {
    return platform().i2c_read(instance, address, data);
}

// One transaction, not two. A register read is a write of the register number
// followed by a read, and the bus must not be released between them: another
// master may take it, and a device that tracks its own address pointer would
// answer from the wrong place. Expressing it as a pair of calls would leave
// that gap open for a later refactor to walk through.
[[nodiscard]] inline Status i2c_write_read(unsigned int instance, unsigned int address,
                                           std::span<const std::byte> command,
                                           std::span<std::byte> data) {
    return platform().i2c_write_read(instance, address, command, data);
}

}
