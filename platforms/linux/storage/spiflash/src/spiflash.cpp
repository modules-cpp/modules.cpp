// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

module platform.linux.storage.spiflash;

import platform.linux.storage.store;

namespace platform::linux::storage {

namespace {

constexpr std::uint8_t manufacturer = 0xef;    // Winbond
constexpr std::uint8_t memory_type = 0x40;     // W25Q, SPI

constexpr std::uint8_t busy_bit = 0x01;
constexpr std::uint8_t wel_bit = 0x02;

[[nodiscard]] bool addressed(std::uint8_t command) {
    switch (command) {
        case 0x03: case 0x0b: case 0x02: case 0x20: case 0x52: case 0xd8: return true;
        default: return false;
    }
}

}  // namespace

SpiFlash::SpiFlash(BlockStore& store) : store_(store) {
    for (std::uint64_t size = store.size(); size > 1; size >>= 1) ++capacity_log2_;
}

bool SpiFlash::fits(std::uint64_t size) {
    return size >= (1u << 16) && size <= (1u << 24) && (size & (size - 1)) == 0;
}

void SpiFlash::chip_select(bool high) {
    const bool selected = !high;
    if (selected == selected_) return;
    selected_ = selected;
    if (selected) {
        count_ = 0;
        command_ = 0;
        ignoring_ = false;
        page_.assign(page_size, 0xff);
        touched_.assign(page_size, false);
    } else if (count_ != 0 && !ignoring_) {
        finish();
    }
}

std::uint8_t SpiFlash::exchange(std::uint8_t in) {
    if (!selected_) return 0xff;
    if (count_ == 0) {
        commands_.push_back(in);
        begin(in);
        count_ = 1;
        return 0xff;
    }
    const auto out = ignoring_ ? std::uint8_t{0xff} : answer(in);
    ++count_;
    return out;
}

void SpiFlash::begin(std::uint8_t command) {
    command_ = command;
    if (powered_down_ && command != 0xab) {
        ignoring_ = true;
        return;
    }
    if (busy_left_ != 0 && command != 0x05) {
        ++busy_violations_;
        ignoring_ = true;
        return;
    }
    if (command != 0x99) reset_enabled_ = false;    // 0x66 must come just before
}

std::uint32_t SpiFlash::address() const {
    return (std::uint32_t{address_[0]} << 16) | (std::uint32_t{address_[1]} << 8) | address_[2];
}

std::uint8_t SpiFlash::status() const {
    return static_cast<std::uint8_t>((busy_left_ != 0 ? busy_bit : 0) |
                                     (write_enabled_ ? wel_bit : 0));
}

// The byte on MISO while the byte in arrives, count_ bytes after the command.
std::uint8_t SpiFlash::answer(std::uint8_t in) {
    if (addressed(command_) && count_ <= 3) {
        address_[count_ - 1] = in;
        if (count_ == 3) cursor_ = address() & static_cast<std::uint32_t>(store_.size() - 1);
        return 0xff;
    }
    switch (command_) {
        case 0x9f: {
            const std::uint8_t id[]{manufacturer, memory_type, capacity_log2_};
            return count_ <= 3 ? id[count_ - 1] : 0xff;
        }
        case 0x05: {
            const auto value = status();
            if (busy_left_ != 0) --busy_left_;
            return value;
        }
        case 0x35: return 0x00;
        case 0xab:
            // Release power-down, then the legacy device ID after three dummies.
            return count_ >= 4 ? static_cast<std::uint8_t>(capacity_log2_ - 1) : 0xff;
        case 0x0b:
            if (count_ == 4) return 0xff;    // the dummy byte
            [[fallthrough]];
        case 0x03: {
            std::byte value{0xff};
            static_cast<void>(store_.read(cursor_, std::span{&value, 1}));
            cursor_ = (cursor_ + 1) & static_cast<std::uint32_t>(store_.size() - 1);
            return static_cast<std::uint8_t>(value);
        }
        case 0x02:
            // The page latch wraps, so later bytes replace earlier ones.
            page_[(cursor_ + count_ - 4) % page_size] = in;
            touched_[(cursor_ + count_ - 4) % page_size] = true;
            return 0xff;
        default: return 0xff;
    }
}

void SpiFlash::erase(std::uint32_t address, std::uint32_t size) {
    const std::uint32_t start = address & ~(size - 1);
    const std::vector<std::byte> erased(size, std::byte{0xff});
    static_cast<void>(store_.write(start, erased));
    ++erases_;
}

// Chip select rose: a command that changes the array or the latches acts now.
void SpiFlash::finish() {
    const bool program_or_erase = command_ == 0x02 || command_ == 0x20 || command_ == 0x52 ||
                                  command_ == 0xd8 || command_ == 0xc7 || command_ == 0x60;
    if (program_or_erase) {
        const bool complete = command_ == 0xc7 || command_ == 0x60 || count_ > 3;
        if (!complete) return;
        if (!write_enabled_) {
            ++unenabled_writes_;
            return;
        }
        if (command_ == 0x02) {
            if (count_ <= 4) return;    // no data: nothing happens
            // Untouched latch bytes are 0xFF, which programs nothing and is
            // not counted.
            const std::uint32_t page_start = cursor_ & ~(page_size - 1);
            std::vector<std::byte> current(page_size);
            static_cast<void>(store_.read(page_start, current));
            for (std::uint32_t i = 0; i < page_size; ++i) {
                if (!touched_[i]) continue;
                const auto old = static_cast<std::uint8_t>(current[i]);
                const auto wanted = page_[i];
                unerased_bits_ += static_cast<unsigned long>(
                    std::popcount(static_cast<std::uint8_t>(~old & wanted)));
                current[i] = static_cast<std::byte>(old & wanted);
            }
            static_cast<void>(store_.write(page_start, current));
            ++programs_;
        } else if (command_ == 0x20) {
            erase(cursor_, sector_size);
        } else if (command_ == 0x52) {
            erase(cursor_, 32 * 1024);
        } else if (command_ == 0xd8) {
            erase(cursor_, 64 * 1024);
        } else {
            erase(0, static_cast<std::uint32_t>(store_.size()));
        }
        write_enabled_ = false;
        busy_left_ = busy_reads_;
        return;
    }
    switch (command_) {
        case 0x06: write_enabled_ = true; return;
        case 0x04: write_enabled_ = false; return;
        case 0xb9: powered_down_ = true; return;
        case 0xab: powered_down_ = false; return;
        case 0x66: reset_enabled_ = true; return;
        case 0x99:
            if (!reset_enabled_) return;
            reset_enabled_ = false;
            write_enabled_ = false;
            powered_down_ = false;
            busy_left_ = 0;
            return;
        default: return;
    }
}

}  // namespace platform::linux::storage
