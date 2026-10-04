// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <span>

export module mm.fs:storage;

import :status;
import :device;

export namespace mm::fs {

// mm.mcu's block storage -- the board-attached device, a USB drive on a
// PIO-USB host port today -- as a BlockDevice. Every call first polls the
// device and answers TransportError while none is ready, so a drive pulled
// between calls is reported rather than read.
class McuStorage final : public BlockDevice {
public:
    [[nodiscard]] Status geometry(BlockGeometry& geometry) override;
    [[nodiscard]] Status read(std::uint64_t block, std::span<std::byte> data) override;
    [[nodiscard]] Status write(std::uint64_t block, std::span<const std::byte> data) override;
    // mm.mcu's storage calls return when the device is done, so there is
    // nothing left to sync.
    [[nodiscard]] Status sync() override;
};

}
