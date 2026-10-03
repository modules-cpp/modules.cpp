// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:storage_types;

export namespace mm::mcu {

// A block device's size: how many blocks, and how many bytes each. A USB
// flash drive answers 512-byte blocks, as SD cards do.
struct StorageGeometry {
    std::uint64_t block_count = 0;
    unsigned int block_size = 0;
};

}
