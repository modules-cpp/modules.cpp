// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// An SD card in SPI mode behind a fake mm.mcu platform: a byte-level model of
// the Physical Layer Simplified Specification's SPI protocol, enough of it
// for mm.sdcard to initialise, read, and write, and to be failed on purpose.
// Each byte the host clocks out is answered by the byte the card would put on
// MISO at the same time.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

import mm.mcu;
import mm.sdcard;

namespace {

enum class Kind { Sdhc, SdscVersion2, SdscVersion1 };

class Card {
public:
    Kind kind = Kind::Sdhc;
    bool present = true;
    bool never_ready = false;
    bool corrupt_next_read = false;
    long pull_after_bytes = -1;    // the card disappears after this many bytes
    std::vector<std::uint8_t> storage = std::vector<std::uint8_t>(2048u * 512u, 0);
    std::vector<unsigned int> commands;    // every command index received
    std::vector<std::uint32_t> arguments;
    unsigned long bytes_seen = 0;

    // A card put back in the socket: powered up afresh, its data kept.
    void insert() {
        present = true;
        pull_after_bytes = -1;
        initialized_ = false;
        crc_enabled_ = false;
        application_ = false;
        polls_ = 0;
        deselect();
    }

    void deselect() {
        frame_.clear();
        output_.clear();
        state_ = State::Command;
    }

    std::uint8_t exchange(std::uint8_t in) {
        ++bytes_seen;
        if (pull_after_bytes >= 0 && static_cast<long>(bytes_seen) > pull_after_bytes) present = false;
        if (!present) return 0xff;
        std::uint8_t out = 0xff;
        if (!output_.empty()) {
            out = output_.front();
            output_.pop_front();
        }
        take(in);
        return out;
    }

private:
    enum class State { Command, Reading, AwaitWriteToken, WriteData, WriteMultipleToken };

    void take(std::uint8_t in) {
        switch (state_) {
            case State::Reading:
                // A multiple read streams blocks until CMD12 arrives.
                if (frame_.empty() && (in & 0xc0u) != 0x40u) {
                    if (output_.size() < 4) queue_block(next_block_++);
                    return;
                }
                frame_.push_back(in);
                if (frame_.size() == 6) {
                    output_.clear();
                    const auto index = frame_[0] & 0x3fu;
                    record(index, argument());
                    frame_.clear();
                    if (index == 12) {
                        output_.push_back(0xff);    // stuff byte
                        output_.push_back(0x00);
                        output_.push_back(0x00);    // a little busy
                        state_ = State::Command;
                    }
                }
                return;
            case State::AwaitWriteToken:
            case State::WriteMultipleToken:
                if (in == 0xfe || in == 0xfc) {
                    data_.clear();
                    state_ = State::WriteData;
                } else if (in == 0xfd && state_ == State::WriteMultipleToken) {
                    output_.push_back(0xff);
                    output_.push_back(0x00);
                    output_.push_back(0x00);
                    state_ = State::Command;
                }
                return;
            case State::WriteData:
                data_.push_back(in);
                if (data_.size() == 514) finish_write();
                return;
            case State::Command:
                break;
        }
        if (frame_.empty() && (in & 0xc0u) != 0x40u) return;
        frame_.push_back(in);
        if (frame_.size() < 6) return;
        const unsigned int index = frame_[0] & 0x3fu;
        const std::uint32_t value = argument();
        const bool crc_ok = check_frame_crc();
        frame_.clear();
        record(index, value);
        respond(index, value, crc_ok);
    }

    [[nodiscard]] std::uint32_t argument() const {
        return (std::uint32_t{frame_[1]} << 24) | (std::uint32_t{frame_[2]} << 16) |
               (std::uint32_t{frame_[3]} << 8) | frame_[4];
    }

    [[nodiscard]] bool check_frame_crc() const {
        std::array<std::byte, 5> head{};
        for (std::size_t i = 0; i < 5; ++i) head[i] = static_cast<std::byte>(frame_[i]);
        return ((mm::sdcard::crc7(head) << 1) | 1u) == frame_[5];
    }

    void record(unsigned int index, std::uint32_t value) {
        commands.push_back(index);
        arguments.push_back(value);
    }

    void r1(std::uint8_t value) {
        output_.push_back(0xff);    // Ncr
        output_.push_back(value);
    }

    [[nodiscard]] std::uint8_t idle_flag() const { return initialized_ ? 0x00 : 0x01; }

    [[nodiscard]] bool sdhc() const { return kind == Kind::Sdhc; }

    [[nodiscard]] std::uint64_t blocks() const { return storage.size() / 512; }

    // An address in blocks, or BadArgument's out-of-range from the card.
    [[nodiscard]] bool block_of(std::uint32_t address, std::uint64_t& block) const {
        if (sdhc()) {
            block = address;
        } else {
            if (address % 512 != 0) return false;
            block = address / 512;
        }
        return block < blocks();
    }

    void respond(unsigned int index, std::uint32_t value, bool crc_ok) {
        const bool crc_checked = index == 0 || index == 8 || crc_enabled_;
        if (crc_checked && !crc_ok) {
            r1(static_cast<std::uint8_t>(idle_flag() | 0x08u));    // CRC error
            return;
        }
        if (application_ && index != 41) application_ = false;
        switch (index) {
            case 0:
                initialized_ = false;
                crc_enabled_ = false;
                polls_ = 0;
                r1(0x01);
                return;
            case 8:
                if (kind == Kind::SdscVersion1) {
                    r1(0x05);    // illegal command: a version 1 card
                    return;
                }
                r1(idle_flag());
                output_.push_back(0x00);
                output_.push_back(0x00);
                output_.push_back(static_cast<std::uint8_t>((value >> 8) & 0x0fu));
                output_.push_back(static_cast<std::uint8_t>(value & 0xffu));
                return;
            case 55:
                application_ = true;
                r1(idle_flag());
                return;
            case 41:
                if (!application_) {
                    r1(static_cast<std::uint8_t>(idle_flag() | 0x04u));
                    return;
                }
                application_ = false;
                if (!never_ready && ++polls_ >= 3) initialized_ = true;
                r1(idle_flag());
                return;
            case 58:
                r1(idle_flag());
                output_.push_back(sdhc() ? 0xc0 : 0x80);    // powered up, CCS for SDHC
                output_.push_back(0xff);
                output_.push_back(0x80);
                output_.push_back(0x00);
                return;
            case 59:
                crc_enabled_ = (value & 1u) != 0;
                r1(idle_flag());
                return;
            case 16:
                r1(value == 512 ? idle_flag() : static_cast<std::uint8_t>(0x40u));
                return;
            case 9:
                r1(idle_flag());
                queue_data(csd());
                return;
            case 13:
                r1(0x00);
                output_.push_back(0x00);
                return;
            case 17:
            case 18: {
                std::uint64_t block = 0;
                if (!block_of(value, block)) {
                    r1(0x40);    // address error
                    return;
                }
                r1(0x00);
                queue_block(block);
                if (index == 18) {
                    next_block_ = block + 1;
                    state_ = State::Reading;
                }
                return;
            }
            case 24:
            case 25: {
                std::uint64_t block = 0;
                if (!block_of(value, block)) {
                    r1(0x40);
                    return;
                }
                r1(0x00);
                write_block_ = block;
                state_ = index == 24 ? State::AwaitWriteToken : State::WriteMultipleToken;
                multiple_ = index == 25;
                return;
            }
            default:
                r1(static_cast<std::uint8_t>(idle_flag() | 0x04u));
                return;
        }
    }

    void queue_data(const std::vector<std::uint8_t>& data) {
        output_.push_back(0xff);
        output_.push_back(0xfe);
        std::vector<std::byte> bytes(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            output_.push_back(data[i]);
            bytes[i] = static_cast<std::byte>(data[i]);
        }
        auto check = mm::sdcard::crc16(bytes);
        if (corrupt_next_read) {
            check ^= 0x1234u;
            corrupt_next_read = false;
        }
        output_.push_back(static_cast<std::uint8_t>(check >> 8));
        output_.push_back(static_cast<std::uint8_t>(check & 0xffu));
    }

    void queue_block(std::uint64_t block) {
        if (block >= blocks()) return;
        std::vector<std::uint8_t> data(storage.begin() + static_cast<std::ptrdiff_t>(block * 512),
                                       storage.begin() + static_cast<std::ptrdiff_t>(block * 512 + 512));
        queue_data(data);
    }

    void finish_write() {
        std::vector<std::byte> bytes(512);
        for (std::size_t i = 0; i < 512; ++i) bytes[i] = static_cast<std::byte>(data_[i]);
        const std::uint16_t sent = static_cast<std::uint16_t>((data_[512] << 8) | data_[513]);
        if (crc_enabled_ && sent != mm::sdcard::crc16(bytes)) {
            output_.push_back(0x0b);    // CRC error
            state_ = State::Command;
            return;
        }
        if (write_block_ < blocks())
            for (std::size_t i = 0; i < 512; ++i) storage[write_block_ * 512 + i] = data_[i];
        ++write_block_;
        output_.push_back(0x05);    // accepted
        output_.push_back(0x00);    // busy
        output_.push_back(0x00);
        state_ = multiple_ ? State::WriteMultipleToken : State::Command;
    }

    // A CSD describing the storage: version 2 for SDHC, version 1 otherwise,
    // with READ_BL_LEN 9 and C_SIZE_MULT 7.
    [[nodiscard]] std::vector<std::uint8_t> csd() const {
        std::vector<std::uint8_t> bytes(16, 0);
        if (sdhc()) {
            const std::uint64_t size = blocks() / 1024 - 1;
            bytes[0] = 0x40;
            bytes[7] = static_cast<std::uint8_t>((size >> 16) & 0x3fu);
            bytes[8] = static_cast<std::uint8_t>(size >> 8);
            bytes[9] = static_cast<std::uint8_t>(size);
        } else {
            const std::uint64_t size = blocks() / 512 - 1;    // (size + 1) * 2^9 * 2^9 / 512
            bytes[0] = 0x00;
            bytes[5] = 0x09;
            bytes[6] = static_cast<std::uint8_t>((size >> 10) & 0x03u);
            bytes[7] = static_cast<std::uint8_t>(size >> 2);
            bytes[8] = static_cast<std::uint8_t>((size & 0x03u) << 6);
            bytes[9] = 0x03;    // C_SIZE_MULT 7: low two bits here,
            bytes[10] = 0x80;   // the high bit here
        }
        bytes[15] = 0x01;
        return bytes;
    }

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
};

constexpr unsigned int chip_select = 23;

class CardPlatform final : public mm::mcu::Platform {
public:
    mm::mcu::Status gpio_configure(unsigned int, mm::mcu::Direction, mm::mcu::Pull) override {
        return mm::mcu::Status::Ok;
    }
    mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (pin == chip_select) {
            if (high && !deselected) card.deselect();
            deselected = high;
        }
        return mm::mcu::Status::Ok;
    }
    mm::mcu::Status spi_configure(const mm::mcu::SpiConfiguration& configuration) override {
        bauds.push_back(configuration.baud);
        return mm::mcu::Status::Ok;
    }
    mm::mcu::Status spi_transfer(unsigned int, std::span<const std::byte> transmit,
                                 std::span<std::byte> receive) override {
        if (transmit.size() != receive.size()) return mm::mcu::Status::BadArgument;
        for (std::size_t i = 0; i < transmit.size(); ++i) {
            if (deselected) {
                receive[i] = std::byte{0xff};
                ++clocks_deselected;
            } else {
                receive[i] = static_cast<std::byte>(card.exchange(static_cast<std::uint8_t>(transmit[i])));
            }
        }
        return mm::mcu::Status::Ok;
    }
    // Time moves on every look at the clock, so every wait in the driver ends.
    mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = ++now;
        return mm::mcu::Status::Ok;
    }
    mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        now += milliseconds;
        return mm::mcu::Status::Ok;
    }

    Card card;
    bool deselected = true;
    unsigned long now = 0;
    unsigned long clocks_deselected = 0;
    std::vector<unsigned long> bauds;
};

CardPlatform platform;

}  // namespace

void mm_test_sdcard_reset(int kind, std::size_t blocks) {
    platform = CardPlatform{};
    platform.card.kind = static_cast<Kind>(kind);
    platform.card.storage.assign(blocks * 512, 0);
    mm::mcu::set_platform(platform);
}
void mm_test_sdcard_present(bool present) {
    if (present) platform.card.insert();
    else platform.card.present = false;
}
void mm_test_sdcard_never_ready(bool value) { platform.card.never_ready = value; }
void mm_test_sdcard_corrupt_next_read() { platform.card.corrupt_next_read = true; }
void mm_test_sdcard_pull_after(long bytes) {
    platform.card.pull_after_bytes = static_cast<long>(platform.card.bytes_seen) + bytes;
}
std::size_t mm_test_sdcard_commands() { return platform.card.commands.size(); }
unsigned int mm_test_sdcard_command(std::size_t i) { return platform.card.commands.at(i); }
std::uint32_t mm_test_sdcard_argument(std::size_t i) { return platform.card.arguments.at(i); }
std::size_t mm_test_sdcard_count(unsigned int index) {
    std::size_t count = 0;
    for (const auto c : platform.card.commands) count += c == index;
    return count;
}
std::uint8_t mm_test_sdcard_storage(std::size_t offset) { return platform.card.storage.at(offset); }
void mm_test_sdcard_store(std::size_t offset, std::uint8_t value) {
    platform.card.storage.at(offset) = value;
}
std::vector<unsigned long> mm_test_sdcard_bauds() { return platform.bauds; }
bool mm_test_sdcard_deselected() { return platform.deselected; }
unsigned long mm_test_sdcard_clocks_deselected() { return platform.clocks_deselected; }
