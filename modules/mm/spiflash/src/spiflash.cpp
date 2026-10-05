// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

module mm.spiflash;

import mm.fs;
import mm.mcu;

namespace mm::spiflash {

namespace {

constexpr std::uint32_t page_size = 256;
constexpr std::uint32_t sector_size = 4096;
constexpr std::uint32_t block_size = 64 * 1024;

constexpr std::byte release_power_down{0xab};
constexpr std::byte jedec{0x9f};
constexpr std::byte read_status{0x05};
constexpr std::byte enable_write{0x06};
constexpr std::byte read_data{0x03};
constexpr std::byte page_program{0x02};
constexpr std::byte sector_erase{0x20};
constexpr std::byte block_erase{0xd8};

constexpr std::byte busy{0x01};
constexpr std::byte write_enabled{0x02};

constexpr unsigned long page_timeout_ms = 10;
constexpr unsigned long sector_timeout_ms = 1'000;
constexpr unsigned long block_timeout_ms = 4'000;
constexpr unsigned long release_timeout_ms = 1;

// The idle level, for transfers that only receive.
constexpr std::array<std::byte, 64> idle_bytes = [] {
    std::array<std::byte, 64> bytes{};
    for (auto& value : bytes) value = std::byte{0xff};
    return bytes;
}();

[[nodiscard]] mm::fs::Status from(mm::mcu::Status status) {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::fs::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::fs::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::fs::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::fs::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::fs::Status::Timeout;
        case mm::mcu::Status::TransportError: return mm::fs::Status::TransportError;
    }
    return mm::fs::Status::TransportError;
}

}  // namespace

Chip::Chip(Wiring wiring) : wiring_(wiring) {}

bool Chip::expired(unsigned long start, unsigned long timeout_ms) const {
    unsigned long now = 0;
    if (mm::mcu::ticks_ms(now) != mm::mcu::Status::Ok) return true;
    return now - start >= timeout_ms;
}

mm::fs::Status Chip::select(bool selected) {
    return from(mm::mcu::gpio_write(wiring_.chip_select_gpio, !selected));
}

mm::fs::Status Chip::exchange(std::span<const std::byte> out, std::span<std::byte> in) {
    return from(mm::mcu::spi_transfer(wiring_.spi.instance, out, in));
}

// Selects the chip and sends an opcode, with a 24-bit address when addressed.
// The chip stays selected for what follows.
mm::fs::Status Chip::command(std::byte opcode, std::uint32_t address, bool addressed) {
    auto status = select(true);
    if (status != mm::fs::Status::Ok) return status;
    const std::array<std::byte, 4> frame{opcode, static_cast<std::byte>(address >> 16),
                                         static_cast<std::byte>(address >> 8),
                                         static_cast<std::byte>(address)};
    std::array<std::byte, 4> ignored{};
    const std::size_t count = addressed ? 4 : 1;
    return exchange(std::span{frame}.first(count), std::span{ignored}.first(count));
}

// An opcode alone, its effect taken when chip select rises.
mm::fs::Status Chip::simple(std::byte opcode) {
    const auto status = command(opcode, 0, false);
    const auto released = select(false);
    return status != mm::fs::Status::Ok ? status : released;
}

mm::fs::Status Chip::status(std::byte& value) {
    auto result = command(read_status, 0, false);
    std::array<std::byte, 1> got{};
    if (result == mm::fs::Status::Ok) result = exchange(std::span{idle_bytes}.first(1), got);
    const auto released = select(false);
    if (result != mm::fs::Status::Ok) return result;
    value = got[0];
    return released;
}

// Write enable, and a status read to see it took: a chip that ignores it
// would ignore the program or erase too, silently.
mm::fs::Status Chip::write_enable() {
    auto result = simple(enable_write);
    if (result != mm::fs::Status::Ok) return result;
    std::byte value{};
    result = status(value);
    if (result != mm::fs::Status::Ok) return result;
    return (value & write_enabled) != std::byte{0} ? mm::fs::Status::Ok
                                                   : mm::fs::Status::TransportError;
}

mm::fs::Status Chip::wait_ready(unsigned long timeout_ms) {
    unsigned long start = 0;
    static_cast<void>(mm::mcu::ticks_ms(start));
    while (true) {
        std::byte value{};
        const auto result = status(value);
        if (result != mm::fs::Status::Ok) return result;
        if ((value & busy) == std::byte{0}) return mm::fs::Status::Ok;
        if (expired(start, timeout_ms)) return mm::fs::Status::Timeout;
    }
}

mm::fs::Status Chip::forget(mm::fs::Status status) {
    ready_ = false;
    static_cast<void>(select(false));
    return status;
}

mm::fs::Status Chip::identify() {
    ready_ = false;
    jedec_id_ = 0;
    auto configuration = wiring_.spi;
    if (configuration.baud == 0) configuration.baud = default_baud;
    configuration.mode = mm::mcu::SpiMode::Mode0;
    configuration.bit_order = mm::mcu::BitOrder::MostSignificantFirst;
    auto status = from(mm::mcu::spi_configure(configuration));
    if (status != mm::fs::Status::Ok) return status;
    status = from(mm::mcu::gpio_configure(wiring_.chip_select_gpio, mm::mcu::Direction::Out,
                                          mm::mcu::Pull::None));
    if (status != mm::fs::Status::Ok) return status;
    status = select(false);
    if (status != mm::fs::Status::Ok) return status;

    // A chip left in power-down answers nothing else; it wakes within 3 us.
    status = simple(release_power_down);
    if (status != mm::fs::Status::Ok) return forget(status);
    if (mm::mcu::delay_us(5) != mm::mcu::Status::Ok)
        static_cast<void>(mm::mcu::delay_ms(release_timeout_ms));
    // A bus with nothing on it reads all ones, which no chip's status is; a
    // chip left mid-erase by a reset finishes first.
    std::byte value{};
    status = this->status(value);
    if (status != mm::fs::Status::Ok) return forget(status);
    if (value == std::byte{0xff}) return forget(mm::fs::Status::TransportError);
    status = wait_ready(block_timeout_ms);
    if (status != mm::fs::Status::Ok) return forget(status);

    status = command(jedec, 0, false);
    std::array<std::byte, 3> id{};
    if (status == mm::fs::Status::Ok) status = exchange(std::span{idle_bytes}.first(3), id);
    if (status != mm::fs::Status::Ok) return forget(status);
    status = select(false);
    if (status != mm::fs::Status::Ok) return forget(status);

    const auto manufacturer = static_cast<unsigned int>(id[0]);
    if (manufacturer == 0x00 || manufacturer == 0xff) return forget(mm::fs::Status::TransportError);
    const auto capacity = static_cast<unsigned int>(id[2]);
    if (capacity < 16 || capacity > 24) return forget(mm::fs::Status::Unsupported);
    jedec_id_ = (manufacturer << 16) | (static_cast<unsigned int>(id[1]) << 8) | capacity;
    size_ = std::uint64_t{1} << capacity;
    ready_ = true;
    return mm::fs::Status::Ok;
}

mm::fs::Status Chip::ready() { return ready_ ? mm::fs::Status::Ok : identify(); }

mm::fs::Status Chip::geometry(mm::fs::FlashGeometry& geometry) {
    const auto status = ready();
    if (status == mm::fs::Status::Ok)
        geometry = {.read_size = 1,
                    .program_size = page_size,
                    .erase_size = sector_size,
                    .erase_count = static_cast<std::uint32_t>(size_ / sector_size)};
    return status;
}

mm::fs::Status Chip::read(std::uint64_t offset, std::span<std::byte> data) {
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    if (offset > size_ || data.size() > size_ - offset) return mm::fs::Status::BadArgument;
    if (data.empty()) return mm::fs::Status::Ok;
    status = command(read_data, static_cast<std::uint32_t>(offset), true);
    while (status == mm::fs::Status::Ok && !data.empty()) {
        const auto count = std::min(data.size(), idle_bytes.size());
        status = exchange(std::span{idle_bytes}.first(count), data.first(count));
        data = data.subspan(count);
    }
    if (status != mm::fs::Status::Ok) return forget(status);
    status = select(false);
    return status != mm::fs::Status::Ok ? forget(status) : status;
}

mm::fs::Status Chip::program(std::uint64_t offset, std::span<const std::byte> data) {
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    if (offset % page_size != 0 || data.size() % page_size != 0 || offset > size_ ||
        data.size() > size_ - offset)
        return mm::fs::Status::BadArgument;
    std::array<std::byte, 64> ignored{};
    for (std::size_t at = 0; at < data.size(); at += page_size) {
        status = write_enable();
        if (status == mm::fs::Status::Ok)
            status = command(page_program, static_cast<std::uint32_t>(offset + at), true);
        for (std::size_t sent = 0; status == mm::fs::Status::Ok && sent < page_size;
             sent += ignored.size())
            status = exchange(data.subspan(at + sent, ignored.size()), ignored);
        if (status == mm::fs::Status::Ok) status = select(false);
        if (status == mm::fs::Status::Ok) status = wait_ready(page_timeout_ms);
        if (status != mm::fs::Status::Ok) return forget(status);
    }
    return mm::fs::Status::Ok;
}

mm::fs::Status Chip::erase(std::uint64_t offset, std::uint64_t size) {
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    if (offset % sector_size != 0 || size % sector_size != 0 || offset > size_ ||
        size > size_ - offset)
        return mm::fs::Status::BadArgument;
    const auto end = offset + size;
    while (offset < end) {
        const bool whole_block = offset % block_size == 0 && end - offset >= block_size;
        status = write_enable();
        if (status == mm::fs::Status::Ok)
            status = command(whole_block ? block_erase : sector_erase,
                             static_cast<std::uint32_t>(offset), true);
        if (status == mm::fs::Status::Ok) status = select(false);
        if (status == mm::fs::Status::Ok)
            status = wait_ready(whole_block ? block_timeout_ms : sector_timeout_ms);
        if (status != mm::fs::Status::Ok) return forget(status);
        offset += whole_block ? block_size : sector_size;
    }
    return mm::fs::Status::Ok;
}

mm::fs::Status Chip::sync() { return mm::fs::Status::Ok; }

}  // namespace mm::spiflash
