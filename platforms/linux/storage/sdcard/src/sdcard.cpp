// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

module platform.linux.storage.sdcard;

import platform.linux.storage.store;

namespace platform::linux::storage {

namespace {

constexpr std::uint64_t block = 512;

// R1 bits.
constexpr std::uint8_t illegal = 0x04;
constexpr std::uint8_t crc_error = 0x08;
constexpr std::uint8_t address_error = 0x20;
constexpr std::uint8_t parameter_error = 0x40;

}  // namespace

std::uint8_t sd_crc7(const std::uint8_t* data, std::size_t size) {
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t d = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint8_t>(crc << 1);
            if (((d ^ crc) & 0x80u) != 0) crc ^= 0x09u;
            d = static_cast<std::uint8_t>(d << 1);
        }
    }
    return static_cast<std::uint8_t>(crc & 0x7fu);
}

std::uint16_t sd_crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(data[i]) << 8));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000u) != 0 ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u)
                                       : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

SdCard::SdCard(BlockStore& store, SdKind kind) : store_(store), kind_(kind) {}

bool SdCard::fits(std::uint64_t size, SdKind kind) {
    if (kind == SdKind::Sdhc) return size != 0 && size % (1024 * block) == 0 && size / (1024 * block) <= 0x400000u;
    return size != 0 && size % (512 * block) == 0 && size <= (1ull << 30);
}

std::uint64_t SdCard::blocks() const { return store_.size() / block; }

void SdCard::reset() {
    frame_.clear();
    output_.clear();
    data_.clear();
    state_ = State::Command;
}

// A select or deselect ends whatever transfer was under way.
void SdCard::chip_select(bool high) {
    const bool selected = !high;
    if (selected != selected_) reset();
    selected_ = selected;
}

void SdCard::remove() {
    present_ = false;
    reset();
}

void SdCard::insert() {
    present_ = true;
    initialized_ = false;
    crc_enabled_ = false;
    application_ = false;
    polls_ = 0;
    reset();
}

std::uint8_t SdCard::exchange(std::uint8_t in) {
    if (!present_ || !selected_) return 0xff;
    std::uint8_t out = 0xff;
    if (!output_.empty()) {
        out = output_.front();
        output_.pop_front();
    }
    take(in);
    return out;
}

void SdCard::take(std::uint8_t in) {
    switch (state_) {
        case State::Reading:
            // A multiple read streams blocks until CMD12 arrives.
            if (frame_.empty() && (in & 0xc0u) != 0x40u) {
                if (output_.size() < 4) queue_block(next_block_++);
                return;
            }
            frame_.push_back(in);
            if (frame_.size() == 6) {
                const unsigned int index = frame_[0] & 0x3fu;
                commands_.push_back(index);
                frame_.clear();
                output_.clear();
                if (index == 12) {
                    output_.push_back(0xff);    // stuff byte
                    output_.push_back(0x00);
                    output_.push_back(0x00);    // busy
                    state_ = State::Command;
                }
            }
            return;
        case State::AwaitWriteToken:
        case State::WriteMultipleToken:
            if (in == 0xfe || (in == 0xfc && state_ == State::WriteMultipleToken)) {
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
            if (data_.size() == block + 2) finish_write();
            return;
        case State::Command:
            break;
    }
    if (frame_.empty() && (in & 0xc0u) != 0x40u) return;
    frame_.push_back(in);
    if (frame_.size() < 6) return;
    const unsigned int index = frame_[0] & 0x3fu;
    const std::uint32_t argument = (std::uint32_t{frame_[1]} << 24) |
                                   (std::uint32_t{frame_[2]} << 16) |
                                   (std::uint32_t{frame_[3]} << 8) | frame_[4];
    const bool crc_ok = ((sd_crc7(frame_.data(), 5) << 1) | 1u) == frame_[5];
    frame_.clear();
    commands_.push_back(index);
    respond(index, argument, crc_ok);
}

void SdCard::r1(std::uint8_t value) {
    output_.push_back(0xff);    // Ncr
    output_.push_back(value);
}

bool SdCard::block_of(std::uint32_t address, std::uint64_t& index) const {
    if (kind_ == SdKind::Sdhc) {
        index = address;
    } else {
        if (address % block != 0) return false;
        index = address / block;
    }
    return index < blocks();
}

void SdCard::respond(unsigned int index, std::uint32_t argument, bool crc_ok) {
    if (index == 0 && !crc_ok) return;    // ignored, as a card ignores it
    if ((index == 8 || crc_enabled_) && !crc_ok) {
        r1(static_cast<std::uint8_t>(idle_flag() | crc_error));
        return;
    }
    const bool application = application_;
    application_ = false;
    // In the idle state only the identification commands are legal.
    if (!initialized_ && index != 0 && index != 1 && index != 8 && index != 41 &&
        index != 55 && index != 58 && index != 59) {
        r1(static_cast<std::uint8_t>(0x01u | illegal));
        return;
    }
    switch (index) {
        case 0:
            initialized_ = false;
            crc_enabled_ = false;
            polls_ = 0;
            r1(0x01);
            return;
        case 8:
            if (kind_ == SdKind::SdscVersion1) {
                r1(static_cast<std::uint8_t>(0x01u | illegal));
                return;
            }
            r1(idle_flag());
            output_.push_back(0x00);
            output_.push_back(0x00);
            output_.push_back(static_cast<std::uint8_t>((argument >> 8) & 0x0fu));
            output_.push_back(static_cast<std::uint8_t>(argument & 0xffu));
            return;
        case 55:
            application_ = true;
            r1(idle_flag());
            return;
        case 41: {
            if (!application) {
                r1(static_cast<std::uint8_t>(idle_flag() | illegal));
                return;
            }
            const bool high_capacity_asked = (argument & 0x4000'0000u) != 0;
            if (kind_ == SdKind::Sdhc && !high_capacity_asked) {
                r1(idle_flag());    // stays idle
                return;
            }
            if (++polls_ >= 3) initialized_ = true;
            r1(idle_flag());
            return;
        }
        case 58:
            r1(idle_flag());
            output_.push_back(kind_ == SdKind::Sdhc ? 0xc0 : 0x80);
            output_.push_back(0xff);
            output_.push_back(0x80);
            output_.push_back(0x00);
            return;
        case 59:
            crc_enabled_ = (argument & 1u) != 0;
            r1(idle_flag());
            return;
        case 16:
            r1(argument == block ? idle_flag() : static_cast<std::uint8_t>(idle_flag() | parameter_error));
            return;
        case 9:
            r1(idle_flag());
            queue_data(csd());
            return;
        case 10:
            r1(idle_flag());
            queue_data(cid());
            return;
        case 13:
            r1(0x00);
            output_.push_back(write_protected_ ? 0x04 : 0x00);    // WP violation
            return;
        case 17:
        case 18: {
            std::uint64_t first = 0;
            if (!block_of(argument, first)) {
                r1(address_error);
                return;
            }
            r1(0x00);
            queue_block(first);
            if (index == 18) {
                next_block_ = first + 1;
                state_ = State::Reading;
            }
            return;
        }
        case 24:
        case 25: {
            std::uint64_t first = 0;
            if (!block_of(argument, first)) {
                r1(address_error);
                return;
            }
            r1(0x00);
            write_block_ = first;
            multiple_ = index == 25;
            state_ = multiple_ ? State::WriteMultipleToken : State::AwaitWriteToken;
            return;
        }
        default:
            r1(static_cast<std::uint8_t>(idle_flag() | illegal));
            return;
    }
}

void SdCard::queue_data(const std::vector<std::uint8_t>& data) {
    output_.push_back(0xff);
    output_.push_back(0xfe);
    for (const auto value : data) output_.push_back(value);
    auto check = sd_crc16(data.data(), data.size());
    if (corrupt_next_read_) {
        check ^= 0x1234u;
        corrupt_next_read_ = false;
    }
    output_.push_back(static_cast<std::uint8_t>(check >> 8));
    output_.push_back(static_cast<std::uint8_t>(check & 0xffu));
}

void SdCard::queue_block(std::uint64_t index) {
    if (index >= blocks()) return;
    std::vector<std::uint8_t> data(block);
    if (!store_.read(index * block, std::as_writable_bytes(std::span{data}))) {
        output_.push_back(0xff);
        output_.push_back(0x08);    // an error token: out of range or failed
        return;
    }
    queue_data(data);
}

void SdCard::finish_write() {
    const std::uint16_t sent = static_cast<std::uint16_t>((data_[block] << 8) | data_[block + 1]);
    std::uint8_t response = 0x05;    // accepted
    if (crc_enabled_ && sent != sd_crc16(data_.data(), block)) {
        response = 0x0b;    // CRC error
    } else if (write_protected_ || writes_left_ == 0 ||
               !store_.write(write_block_ * block,
                             std::as_bytes(std::span{data_}.first(block)))) {
        response = 0x0d;    // write error
    } else {
        if (writes_left_ > 0) --writes_left_;
        ++write_block_;
    }
    output_.push_back(response);
    output_.push_back(0x00);    // busy
    output_.push_back(0x00);
    state_ = multiple_ && response == 0x05 ? State::WriteMultipleToken : State::Command;
}

std::vector<std::uint8_t> SdCard::csd() const {
    std::vector<std::uint8_t> bytes(16, 0);
    if (kind_ == SdKind::Sdhc) {
        const std::uint64_t size = blocks() / 1024 - 1;
        bytes[0] = 0x40;
        bytes[7] = static_cast<std::uint8_t>((size >> 16) & 0x3fu);
        bytes[8] = static_cast<std::uint8_t>(size >> 8);
        bytes[9] = static_cast<std::uint8_t>(size);
    } else {
        const std::uint64_t size = blocks() / 512 - 1;
        bytes[5] = 0x09;
        bytes[6] = static_cast<std::uint8_t>((size >> 10) & 0x03u);
        bytes[7] = static_cast<std::uint8_t>(size >> 2);
        bytes[8] = static_cast<std::uint8_t>((size & 0x03u) << 6);
        bytes[9] = 0x03;    // C_SIZE_MULT 7
        bytes[10] = 0x80;
    }
    bytes[15] = static_cast<std::uint8_t>((sd_crc7(bytes.data(), 15) << 1) | 1u);
    return bytes;
}

std::vector<std::uint8_t> SdCard::cid() const {
    std::vector<std::uint8_t> bytes{0x00, 'm', 'm', 'E', 'M', 'U', 'S', 'D', 0x10,
                                    0x00, 0x00, 0x00, 0x01, 0x01, 0xa6, 0x00};
    bytes[15] = static_cast<std::uint8_t>((sd_crc7(bytes.data(), 15) << 1) | 1u);
    return bytes;
}

}  // namespace platform::linux::storage
