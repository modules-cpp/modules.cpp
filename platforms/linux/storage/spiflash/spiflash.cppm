// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module platform.linux.storage.spiflash;

import platform.linux.storage.store;

export namespace platform::linux::storage {

class SpiFlash {
public:
    static constexpr std::uint32_t page_size = 256;
    static constexpr std::uint32_t sector_size = 4096;

    explicit SpiFlash(BlockStore& store);

    // Whether size is a power of two from 64 KiB to 16 MiB.
    [[nodiscard]] static bool fits(std::uint64_t size);

    // Chip select, active low: false selects the chip, and its rising edge
    // ends the command, which is when a program or erase takes effect.
    void chip_select(bool high);
    [[nodiscard]] std::uint8_t exchange(std::uint8_t in);

    // How many status reads report BUSY after a program or erase; at least 1.
    void busy_reads(unsigned int count) { busy_reads_ = count == 0 ? 1 : count; }

    [[nodiscard]] bool busy() const { return busy_left_ != 0; }
    [[nodiscard]] bool write_enabled() const { return write_enabled_; }
    [[nodiscard]] bool powered_down() const { return powered_down_; }
    // Bits a program tried to set from 0 to 1.
    [[nodiscard]] unsigned long unerased_bits() const { return unerased_bits_; }
    // Programs and erases sent without write enable.
    [[nodiscard]] unsigned long unenabled_writes() const { return unenabled_writes_; }
    // Commands sent while busy, other than status reads.
    [[nodiscard]] unsigned long busy_violations() const { return busy_violations_; }
    [[nodiscard]] unsigned long programs() const { return programs_; }
    [[nodiscard]] unsigned long erases() const { return erases_; }
    [[nodiscard]] const std::vector<std::uint8_t>& commands() const { return commands_; }

private:
    void begin(std::uint8_t command);
    [[nodiscard]] std::uint8_t answer(std::uint8_t in);
    void finish();
    void erase(std::uint32_t address, std::uint32_t size);
    [[nodiscard]] std::uint32_t address() const;
    [[nodiscard]] std::uint8_t status() const;

    BlockStore& store_;
    std::uint8_t capacity_log2_ = 0;
    bool selected_ = false;
    bool write_enabled_ = false;
    bool powered_down_ = false;
    bool reset_enabled_ = false;
    unsigned int busy_reads_ = 1;
    unsigned int busy_left_ = 0;
    bool ignoring_ = false;
    std::uint8_t command_ = 0;
    std::size_t count_ = 0;          // bytes since the command byte
    std::uint8_t address_[3]{};
    std::uint32_t cursor_ = 0;
    std::vector<std::uint8_t> page_;
    std::vector<bool> touched_;    // which page_ bytes the program sent
    unsigned long unerased_bits_ = 0;
    unsigned long unenabled_writes_ = 0;
    unsigned long busy_violations_ = 0;
    unsigned long programs_ = 0;
    unsigned long erases_ = 0;
    std::vector<std::uint8_t> commands_;
};

}  // namespace platform::linux::storage
