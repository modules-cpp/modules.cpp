// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <span>

export module mm.fs:device;

import :status;

export namespace mm::fs {

// Fixed-size blocks rewritten in place, as mm.mcu storage and SD cards present
// them. FAT wants this.
struct BlockGeometry {
    std::uint64_t count = 0;
    unsigned int size = 0;       // bytes
};

// Every method answers Unsupported by default. read and write move whole
// blocks from block on, as many as data holds.
class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    [[nodiscard]] virtual Status geometry(BlockGeometry&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status read(std::uint64_t, std::span<std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status write(std::uint64_t, std::span<const std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status sync() { return Status::Unsupported; }
};

// Raw flash, as littlefs wants it: read anywhere, program only what was
// erased, erase only whole erase blocks. A different promise from a block
// device's, which is why mm.fs keeps both.
struct FlashGeometry {
    std::uint32_t read_size = 1;       // smallest read, bytes
    std::uint32_t program_size = 0;    // smallest program, bytes
    std::uint32_t erase_size = 0;      // one erase block, bytes
    std::uint32_t erase_count = 0;     // erase blocks in the device
};

// Every method answers Unsupported by default. Offsets are bytes within the
// device; program and erase must respect the geometry's sizes.
class FlashDevice {
public:
    virtual ~FlashDevice() = default;

    [[nodiscard]] virtual Status geometry(FlashGeometry&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status read(std::uint64_t, std::span<std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status program(std::uint64_t, std::span<const std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status erase(std::uint64_t, std::uint64_t) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status sync() { return Status::Unsupported; }
};

}
