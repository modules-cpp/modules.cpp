// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:storage;

import :status;
import :storage_types;
import :platform;

export namespace mm::mcu {

// Block storage the board attaches, such as a USB flash drive on a USB host
// port: one device, addressed in whole blocks.
//
// Nothing services the device between calls. storage_poll lets the platform
// do its work for a moment and reports whether a device is ready, which is how
// a program notices one arriving or leaving; a program that waits for a drive
// polls. present changes only on Ok.
[[nodiscard]] inline Status storage_poll(bool& present) {
    return platform().storage_poll(present);
}

// The ready device's size. TransportError while none is ready, as for every
// call below: there is nothing on the other end. geometry changes only on Ok.
[[nodiscard]] inline Status storage_geometry(StorageGeometry& geometry) {
    return platform().storage_geometry(geometry);
}

// Reads or writes whole blocks from block on, as many as data holds, and
// returns when they are done or failed, so a call waits for the device. An
// empty span is BadArgument here; a size that is not a whole number of blocks,
// or blocks past the device's end, are BadArgument from the platform before
// it asks the device. On failure a read may have filled part of data and a
// write may have written part of the blocks: a block device promises no more.
[[nodiscard]] inline Status storage_read(std::uint64_t block, std::span<std::byte> data) {
    if (data.empty()) return Status::BadArgument;
    return platform().storage_read(block, data);
}

[[nodiscard]] inline Status storage_write(std::uint64_t block,
                                          std::span<const std::byte> data) {
    if (data.empty()) return Status::BadArgument;
    return platform().storage_write(block, data);
}

}
