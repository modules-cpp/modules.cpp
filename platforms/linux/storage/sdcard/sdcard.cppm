// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

export module platform.linux.storage.sdcard;

import platform.linux.storage.store;

export namespace platform::linux::storage {

enum class SdKind { Sdhc, SdscVersion2, SdscVersion1 };

class SdCard {
public:
    SdCard(BlockStore& store, SdKind kind);

    // Whether the store's size is one this kind of card can describe.
    [[nodiscard]] static bool fits(std::uint64_t size, SdKind kind);

    // Chip select, active low: false selects the card.
    void chip_select(bool high);
    [[nodiscard]] std::uint8_t exchange(std::uint8_t in);

    void remove();
    void insert();
    [[nodiscard]] bool present() const { return present_; }
    void write_protect(bool value) { write_protected_ = value; }
    void fail_writes_after(long blocks) { writes_left_ = blocks; }
    void corrupt_next_read() { corrupt_next_read_ = true; }

    [[nodiscard]] bool crc_enabled() const { return crc_enabled_; }
    [[nodiscard]] const std::vector<unsigned int>& commands() const { return commands_; }

private:
    enum class State { Command, Reading, AwaitWriteToken, WriteData, WriteMultipleToken };

    void take(std::uint8_t in);
    void respond(unsigned int index, std::uint32_t argument, bool crc_ok);
    void r1(std::uint8_t value);
    [[nodiscard]] std::uint8_t idle_flag() const { return initialized_ ? 0x00 : 0x01; }
    [[nodiscard]] bool block_of(std::uint32_t address, std::uint64_t& block) const;
    [[nodiscard]] std::uint64_t blocks() const;
    void queue_data(const std::vector<std::uint8_t>& data);
    void queue_block(std::uint64_t block);
    void finish_write();
    [[nodiscard]] std::vector<std::uint8_t> csd() const;
    [[nodiscard]] std::vector<std::uint8_t> cid() const;
    void reset();

    BlockStore& store_;
    SdKind kind_;
    bool present_ = true;
    bool selected_ = false;
    bool write_protected_ = false;
    long writes_left_ = -1;
    bool corrupt_next_read_ = false;
    std::vector<std::uint8_t> frame_;
    std::deque<std::uint8_t> output_;
    std::vector<std::uint8_t> data_;
    State state_ = State::Command;
    bool initialized_ = false;
    bool crc_enabled_ = false;
    bool application_ = false;
    bool multiple_ = false;
    unsigned int polls_ = 0;
    std::uint64_t next_block_ = 0;
    std::uint64_t write_block_ = 0;
    std::vector<unsigned int> commands_;
};

// The SD CRCs: CRC7 over a command's first five bytes, CRC16-CCITT over data.
[[nodiscard]] std::uint8_t sd_crc7(const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::uint16_t sd_crc16(const std::uint8_t* data, std::size_t size);

}  // namespace platform::linux::storage
