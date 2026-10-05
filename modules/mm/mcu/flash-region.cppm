// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:flash_region;

import :status;
import :flash_region_types;
import :platform;

export namespace mm::mcu {

// The region's size and granularity. Unsupported where the platform has no
// region, including a board whose region is zero bytes.
[[nodiscard]] inline Status flash_region_geometry(FlashRegionGeometry& geometry) {
    return platform().flash_region_geometry(geometry);
}

// Offsets are bytes from the start of the region, and each call waits until
// the flash is done. A read may start and end anywhere inside the region. A
// program must start and end on program_size and lands only on erased bytes;
// an erase must start and end on erase_size. An empty transfer, a misaligned
// one, or one past the region's end is BadArgument.
//
// On a Pico the region is the chip that holds the program: while a program
// or erase runs, nothing executes from flash and interrupts are held off on
// the calling core, for tens of milliseconds per erase block. A USB console
// rides through that; a PIO-USB host transfer does not.
[[nodiscard]] inline Status flash_region_read(std::uint64_t offset, std::span<std::byte> data) {
    if (data.empty()) return Status::BadArgument;
    return platform().flash_region_read(offset, data);
}

[[nodiscard]] inline Status flash_region_program(std::uint64_t offset,
                                                 std::span<const std::byte> data) {
    if (data.empty()) return Status::BadArgument;
    return platform().flash_region_program(offset, data);
}

[[nodiscard]] inline Status flash_region_erase(std::uint64_t offset, std::uint64_t size) {
    if (size == 0) return Status::BadArgument;
    return platform().flash_region_erase(offset, size);
}

}
