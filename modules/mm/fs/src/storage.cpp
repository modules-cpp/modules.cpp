// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <span>

module mm.fs;

import mm.mcu;

namespace mm::fs {

namespace {

[[nodiscard]] Status from(mm::mcu::Status status) {
    switch (status) {
        case mm::mcu::Status::Ok: return Status::Ok;
        case mm::mcu::Status::BadArgument: return Status::BadArgument;
        case mm::mcu::Status::Unsupported: return Status::Unsupported;
        case mm::mcu::Status::Busy: return Status::Busy;
        case mm::mcu::Status::Timeout: return Status::Timeout;
        case mm::mcu::Status::TransportError: return Status::TransportError;
    }
    return Status::TransportError;
}

[[nodiscard]] Status ready() {
    bool present = false;
    const auto polled = from(mm::mcu::storage_poll(present));
    if (polled != Status::Ok) return polled;
    return present ? Status::Ok : Status::TransportError;
}

}  // namespace

Status McuStorage::geometry(BlockGeometry& geometry) {
    const auto status = ready();
    if (status != Status::Ok) return status;
    mm::mcu::StorageGeometry device;
    const auto read = from(mm::mcu::storage_geometry(device));
    if (read == Status::Ok) geometry = {device.block_count, device.block_size};
    return read;
}

Status McuStorage::read(std::uint64_t block, std::span<std::byte> data) {
    const auto status = ready();
    if (status != Status::Ok) return status;
    return from(mm::mcu::storage_read(block, data));
}

Status McuStorage::write(std::uint64_t block, std::span<const std::byte> data) {
    const auto status = ready();
    if (status != Status::Ok) return status;
    return from(mm::mcu::storage_write(block, data));
}

Status McuStorage::sync() { return Status::Ok; }

}  // namespace mm::fs
