// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.sdcard;

import mm.fs;
import mm.mcu;

export namespace mm::sdcard {

// The card's bus: an SPI instance with its clock, its MOSI (the card's CMD
// line), and its MISO (the card's DAT0), and the chip select (the card's
// DAT3). spi.baud is ignored: identification runs at 400 kHz and data at
// data_baud, which SPI mode allows up to 25 MHz.
struct SpiWiring {
    mm::mcu::SpiConfiguration spi;
    unsigned int chip_select_gpio = 0;
    unsigned long data_baud = 25'000'000;
};

class SpiCard final : public mm::fs::BlockDevice {
public:
    explicit SpiCard(SpiWiring wiring);

    // TransportError when no card answers; Unsupported for a card that is
    // not an SD card or does not take this voltage; Timeout for one that
    // never leaves idle.
    [[nodiscard]] mm::fs::Status geometry(mm::fs::BlockGeometry& geometry) override;
    [[nodiscard]] mm::fs::Status read(std::uint64_t block, std::span<std::byte> data) override;
    [[nodiscard]] mm::fs::Status write(std::uint64_t block,
                                       std::span<const std::byte> data) override;
    // Writes complete before write returns, so there is nothing to sync.
    [[nodiscard]] mm::fs::Status sync() override;

private:
    [[nodiscard]] mm::fs::Status ready();
    [[nodiscard]] mm::fs::Status initialize();
    [[nodiscard]] mm::fs::Status configure(unsigned long baud);
    [[nodiscard]] mm::fs::Status select(bool selected);
    [[nodiscard]] mm::fs::Status exchange(std::span<const std::byte> out, std::span<std::byte> in);
    [[nodiscard]] mm::fs::Status receive(std::span<std::byte> in);
    [[nodiscard]] mm::fs::Status byte(std::byte out, std::byte& in);
    [[nodiscard]] mm::fs::Status wait_ready(unsigned long timeout_ms);
    [[nodiscard]] mm::fs::Status command(unsigned int index, std::uint32_t argument,
                                         std::byte& response);
    [[nodiscard]] mm::fs::Status application_command(unsigned int index, std::uint32_t argument,
                                                     std::byte& response);
    [[nodiscard]] mm::fs::Status receive_block(std::span<std::byte> data);
    [[nodiscard]] mm::fs::Status send_block(std::byte token, std::span<const std::byte> data);
    [[nodiscard]] mm::fs::Status finish(mm::fs::Status status);
    [[nodiscard]] mm::fs::Status forget(mm::fs::Status status);
    [[nodiscard]] bool expired(unsigned long start, unsigned long timeout_ms) const;

    SpiWiring wiring_;
    bool ready_ = false;
    bool block_addressed_ = false;
    std::uint64_t block_count_ = 0;
};

// The card's native bus: an mm.mcu sdio instance on the clock, the command
// line, and D0 to D3 on the four GPIOs from data0_gpio. Identification runs at
// 400 kHz; data in 4-bit mode at data_clock_hz, which default speed allows up
// to 25 MHz.
struct SdioWiring {
    unsigned int instance = 0;
    unsigned int clock_gpio = 0;
    unsigned int command_gpio = 0;
    unsigned int data0_gpio = 0;
    unsigned long data_clock_hz = 25'000'000;
};

class SdioCard final : public mm::fs::BlockDevice {
public:
    explicit SdioCard(SdioWiring wiring);

    // TransportError when no card answers; Unsupported for a card that is
    // not an SD card or does not take this voltage, or a platform without an
    // sdio facility; Timeout for one that never leaves busy.
    [[nodiscard]] mm::fs::Status geometry(mm::fs::BlockGeometry& geometry) override;
    [[nodiscard]] mm::fs::Status read(std::uint64_t block, std::span<std::byte> data) override;
    [[nodiscard]] mm::fs::Status write(std::uint64_t block,
                                       std::span<const std::byte> data) override;
    // Writes complete before write returns, so there is nothing to sync.
    [[nodiscard]] mm::fs::Status sync() override;

    // The data clock the platform gave, once the card is identified; zero
    // before.
    [[nodiscard]] unsigned long data_clock_hz() const { return clock_hz_; }

private:
    [[nodiscard]] mm::fs::Status ready();
    [[nodiscard]] mm::fs::Status initialize();
    [[nodiscard]] mm::fs::Status command(unsigned int index, std::uint32_t argument,
                                         mm::mcu::SdioResponse response,
                                         std::span<std::uint32_t> words);
    [[nodiscard]] mm::fs::Status status_command(unsigned int index, std::uint32_t argument,
                                                mm::mcu::SdioResponse response);
    [[nodiscard]] mm::fs::Status application_command(unsigned int index, std::uint32_t argument,
                                                     mm::mcu::SdioResponse response,
                                                     std::uint32_t& word);
    [[nodiscard]] mm::fs::Status forget(mm::fs::Status status);
    [[nodiscard]] bool expired(unsigned long start, unsigned long timeout_ms) const;

    SdioWiring wiring_;
    bool ready_ = false;
    bool block_addressed_ = false;
    std::uint64_t block_count_ = 0;
    std::uint32_t rca_ = 0;
    unsigned long clock_hz_ = 0;
    // A block for reads into a buffer the facility cannot take, one not on
    // a four-byte boundary.
    alignas(4) std::byte bounce_[512]{};
};

// A card's capacity in 512-byte blocks from its CSD, sixteen bytes most
// significant first: version 1 from C_SIZE, C_SIZE_MULT, and READ_BL_LEN,
// version 2 from its 22-bit C_SIZE. False for a CSD structure neither.
[[nodiscard]] bool csd_block_count(std::span<const std::byte> csd, std::uint64_t& blocks);

// The SD CRCs, exposed for tests and for anything else speaking the protocol:
// CRC7 over a command's first five bytes, and CRC16-CCITT over a data block.
[[nodiscard]] std::uint8_t crc7(std::span<const std::byte> data);
[[nodiscard]] std::uint16_t crc16(std::span<const std::byte> data);

}
