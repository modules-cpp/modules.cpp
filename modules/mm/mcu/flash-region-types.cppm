// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:flash_region_types;

export namespace mm::mcu {

// A region of raw flash set aside for data: the top of a Pico's program
// flash, or an image file a Linux device map names. size is a whole number of
// erase blocks; a read may start anywhere, a program must cover whole
// program_size units, and an erase whole erase_size blocks.
struct FlashRegionGeometry {
    std::uint64_t size = 0;
    std::uint32_t read_size = 1;
    std::uint32_t program_size = 0;
    std::uint32_t erase_size = 0;
};

}
