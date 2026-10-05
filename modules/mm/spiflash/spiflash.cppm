// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.spiflash;

import mm.fs;
import mm.mcu;

export namespace mm::spiflash {

// The chip's bus: an SPI instance with its pins, and the chip select. A
// spi.baud of zero is default_baud; mode and bit order are the chip's, mode 0
// and most significant first, whatever spi says.
struct Wiring {
    mm::mcu::SpiConfiguration spi;
    unsigned int chip_select_gpio = 0;
};

inline constexpr unsigned long default_baud = 20'000'000;

class Chip final : public mm::fs::FlashDevice {
public:
    explicit Chip(Wiring wiring);

    // read_size 1, program_size 256, erase_size 4096, and the erase blocks
    // the JEDEC capacity gives. TransportError when no chip answers;
    // Unsupported for a capacity outside 64 KiB to 16 MiB.
    [[nodiscard]] mm::fs::Status geometry(mm::fs::FlashGeometry& geometry) override;
    [[nodiscard]] mm::fs::Status read(std::uint64_t offset, std::span<std::byte> data) override;
    // offset and data's size whole pages.
    [[nodiscard]] mm::fs::Status program(std::uint64_t offset,
                                         std::span<const std::byte> data) override;
    // offset and size whole sectors.
    [[nodiscard]] mm::fs::Status erase(std::uint64_t offset, std::uint64_t size) override;
    // Programs and erases complete before they return, so there is nothing
    // to sync.
    [[nodiscard]] mm::fs::Status sync() override;

    // The JEDEC ID read at identification: manufacturer, memory type,
    // capacity. Zero until the chip has been identified.
    [[nodiscard]] std::uint32_t jedec_id() const { return jedec_id_; }

private:
    [[nodiscard]] mm::fs::Status ready();
    [[nodiscard]] mm::fs::Status identify();
    [[nodiscard]] mm::fs::Status select(bool selected);
    [[nodiscard]] mm::fs::Status exchange(std::span<const std::byte> out, std::span<std::byte> in);
    [[nodiscard]] mm::fs::Status command(std::byte opcode, std::uint32_t address,
                                         bool addressed);
    [[nodiscard]] mm::fs::Status simple(std::byte opcode);
    [[nodiscard]] mm::fs::Status status(std::byte& value);
    [[nodiscard]] mm::fs::Status write_enable();
    [[nodiscard]] mm::fs::Status wait_ready(unsigned long timeout_ms);
    [[nodiscard]] mm::fs::Status forget(mm::fs::Status status);
    [[nodiscard]] bool expired(unsigned long start, unsigned long timeout_ms) const;

    Wiring wiring_;
    bool ready_ = false;
    std::uint32_t jedec_id_ = 0;
    std::uint64_t size_ = 0;
};

}
