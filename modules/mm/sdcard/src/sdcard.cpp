// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

module mm.sdcard;

import mm.fs;
import mm.mcu;

namespace mm::sdcard {

namespace {

constexpr std::size_t block_size = 512;
constexpr unsigned long identification_baud = 400'000;
constexpr unsigned int reset_attempts = 10;
constexpr unsigned long idle_timeout_ms = 1'000;
constexpr unsigned long token_timeout_ms = 200;
constexpr unsigned long busy_timeout_ms = 500;

constexpr std::byte idle_byte{0xff};
constexpr std::byte start_block{0xfe};
constexpr std::byte start_multiple{0xfc};
constexpr std::byte stop_multiple{0xfd};

// R1 flags.
constexpr std::byte in_idle{0x01};
constexpr std::byte illegal_command{0x04};

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

[[nodiscard]] unsigned int bits(std::span<const std::byte> bytes, unsigned int index) {
    return static_cast<unsigned int>(bytes[index]);
}

}  // namespace

bool csd_block_count(std::span<const std::byte> csd, std::uint64_t& blocks) {
    if (csd.size() < 16) return false;
    const unsigned int structure = bits(csd, 0) >> 6;
    if (structure == 1) {
        const std::uint64_t size = ((bits(csd, 7) & 0x3fu) << 16) | (bits(csd, 8) << 8) |
                                   bits(csd, 9);
        blocks = (size + 1) * 1024;
        return true;
    }
    if (structure == 0) {
        const unsigned int read_length = bits(csd, 5) & 0x0fu;
        const std::uint64_t size = ((bits(csd, 6) & 0x03u) << 10) | (bits(csd, 7) << 2) |
                                   (bits(csd, 8) >> 6);
        const unsigned int multiplier = ((bits(csd, 9) & 0x03u) << 1) | (bits(csd, 10) >> 7);
        blocks = ((size + 1) << (multiplier + 2) << read_length) / block_size;
        return true;
    }
    return false;
}

std::uint8_t crc7(std::span<const std::byte> data) {
    std::uint8_t crc = 0;
    for (const auto value : data) {
        auto d = static_cast<std::uint8_t>(value);
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint8_t>(crc << 1);
            if (((d ^ crc) & 0x80u) != 0) crc ^= 0x09u;
            d = static_cast<std::uint8_t>(d << 1);
        }
    }
    return static_cast<std::uint8_t>(crc & 0x7fu);
}

std::uint16_t crc16(std::span<const std::byte> data) {
    std::uint16_t crc = 0;
    for (const auto value : data) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(value) << 8));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000u) != 0 ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u)
                                       : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

SpiCard::SpiCard(SpiWiring wiring) : wiring_(wiring) {}

bool SpiCard::expired(unsigned long start, unsigned long timeout_ms) const {
    unsigned long now = 0;
    if (mm::mcu::ticks_ms(now) != mm::mcu::Status::Ok) return true;
    return now - start >= timeout_ms;
}

mm::fs::Status SpiCard::configure(unsigned long baud) {
    auto configuration = wiring_.spi;
    configuration.baud = baud;
    configuration.mode = mm::mcu::SpiMode::Mode0;
    configuration.bit_order = mm::mcu::BitOrder::MostSignificantFirst;
    return from(mm::mcu::spi_configure(configuration));
}

mm::fs::Status SpiCard::exchange(std::span<const std::byte> out, std::span<std::byte> in) {
    return from(mm::mcu::spi_transfer(wiring_.spi.instance, out, in));
}

// Clocks the idle level out while data comes in, a chunk at a time.
mm::fs::Status SpiCard::receive(std::span<std::byte> in) {
    while (!in.empty()) {
        const auto count = std::min(in.size(), idle_bytes.size());
        const auto status = exchange(std::span{idle_bytes}.first(count), in.first(count));
        if (status != mm::fs::Status::Ok) return status;
        in = in.subspan(count);
    }
    return mm::fs::Status::Ok;
}

mm::fs::Status SpiCard::byte(std::byte out, std::byte& in) {
    const std::array<std::byte, 1> sent{out};
    std::array<std::byte, 1> got{};
    const auto status = exchange(sent, got);
    if (status == mm::fs::Status::Ok) in = got[0];
    return status;
}

// Chip select low begins a transaction; high ends it, and one more byte of
// clocks lets the card release its data line.
mm::fs::Status SpiCard::select(bool selected) {
    const auto status = from(mm::mcu::gpio_write(wiring_.chip_select_gpio, !selected));
    if (status != mm::fs::Status::Ok || selected) return status;
    std::byte ignored{};
    return byte(idle_byte, ignored);
}

// A card holds its data line low while busy.
mm::fs::Status SpiCard::wait_ready(unsigned long timeout_ms) {
    unsigned long start = 0;
    static_cast<void>(mm::mcu::ticks_ms(start));
    while (true) {
        std::byte got{};
        const auto status = byte(idle_byte, got);
        if (status != mm::fs::Status::Ok) return status;
        if (got == idle_byte) return mm::fs::Status::Ok;
        if (expired(start, timeout_ms)) return mm::fs::Status::Timeout;
    }
}

// One command frame -- start bit and index, the argument, CRC7 and end bit --
// and its R1, which arrives within eight bytes with its top bit clear. A card
// that never answers is TransportError: nothing is there.
mm::fs::Status SpiCard::command(unsigned int index, std::uint32_t argument,
                                std::byte& response) {
    if (index != 0) {
        const auto waited = wait_ready(busy_timeout_ms);
        if (waited != mm::fs::Status::Ok) return waited;
    }
    std::array<std::byte, 6> frame{
        static_cast<std::byte>(0x40u | (index & 0x3fu)),
        static_cast<std::byte>(argument >> 24),
        static_cast<std::byte>(argument >> 16),
        static_cast<std::byte>(argument >> 8),
        static_cast<std::byte>(argument),
        std::byte{0}};
    frame[5] = static_cast<std::byte>((crc7(std::span{frame}.first(5)) << 1) | 1u);
    std::array<std::byte, 6> ignored{};
    auto status = exchange(frame, ignored);
    if (status != mm::fs::Status::Ok) return status;
    if (index == 12) {    // CMD12 is followed by a stuff byte
        std::byte stuff{};
        status = byte(idle_byte, stuff);
        if (status != mm::fs::Status::Ok) return status;
    }
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::byte got{};
        status = byte(idle_byte, got);
        if (status != mm::fs::Status::Ok) return status;
        if ((got & std::byte{0x80}) == std::byte{0}) {
            response = got;
            return mm::fs::Status::Ok;
        }
    }
    return mm::fs::Status::TransportError;
}

mm::fs::Status SpiCard::application_command(unsigned int index, std::uint32_t argument,
                                            std::byte& response) {
    const auto status = command(55, 0, response);
    if (status != mm::fs::Status::Ok) return status;
    if ((response & ~in_idle) != std::byte{0}) return mm::fs::Status::Ok;
    return command(index, argument, response);
}

// A data block: the start token, the data, and its CRC16, which must match.
mm::fs::Status SpiCard::receive_block(std::span<std::byte> data) {
    unsigned long start = 0;
    static_cast<void>(mm::mcu::ticks_ms(start));
    std::byte token{0xff};
    while (true) {
        const auto status = byte(idle_byte, token);
        if (status != mm::fs::Status::Ok) return status;
        if (token != idle_byte) break;
        if (expired(start, token_timeout_ms)) return mm::fs::Status::Timeout;
    }
    if (token != start_block) return mm::fs::Status::TransportError;    // an error token
    auto status = receive(data);
    if (status != mm::fs::Status::Ok) return status;
    std::array<std::byte, 2> check{};
    status = receive(check);
    if (status != mm::fs::Status::Ok) return status;
    const auto expected = crc16(data);
    if (static_cast<std::uint16_t>((bits(check, 0) << 8) | bits(check, 1)) != expected)
        return mm::fs::Status::TransportError;
    return mm::fs::Status::Ok;
}

mm::fs::Status SpiCard::send_block(std::byte token, std::span<const std::byte> data) {
    std::byte ignored{};
    auto status = byte(idle_byte, ignored);
    if (status == mm::fs::Status::Ok) status = byte(token, ignored);
    std::array<std::byte, 64> discard{};
    for (std::size_t at = 0; status == mm::fs::Status::Ok && at < data.size();
         at += discard.size()) {
        const auto count = std::min(discard.size(), data.size() - at);
        status = exchange(data.subspan(at, count), std::span{discard}.first(count));
    }
    if (status != mm::fs::Status::Ok) return status;
    const auto check = crc16(data);
    status = byte(static_cast<std::byte>(check >> 8), ignored);
    if (status == mm::fs::Status::Ok) status = byte(static_cast<std::byte>(check & 0xffu), ignored);
    if (status != mm::fs::Status::Ok) return status;
    std::byte response{};
    status = byte(idle_byte, response);
    if (status != mm::fs::Status::Ok) return status;
    // 0bxxx00101 is accepted; a CRC error or a write error is not.
    if ((response & std::byte{0x1f}) != std::byte{0x05}) return mm::fs::Status::TransportError;
    return wait_ready(busy_timeout_ms);
}

mm::fs::Status SpiCard::finish(mm::fs::Status status) {
    const auto released = select(false);
    return status != mm::fs::Status::Ok ? status : released;
}

mm::fs::Status SpiCard::forget(mm::fs::Status status) {
    ready_ = false;
    static_cast<void>(select(false));
    return status;
}

mm::fs::Status SpiCard::initialize() {
    ready_ = false;
    auto status = configure(identification_baud);
    if (status != mm::fs::Status::Ok) return status;
    status = from(mm::mcu::gpio_configure(wiring_.chip_select_gpio, mm::mcu::Direction::Out,
                                          mm::mcu::Pull::None));
    if (status != mm::fs::Status::Ok) return status;
    status = from(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::fs::Status::Ok) return status;
    std::array<std::byte, 10> wake{};
    status = receive(wake);    // 80 clocks with chip select high
    if (status != mm::fs::Status::Ok) return status;

    status = select(true);
    if (status != mm::fs::Status::Ok) return status;
    std::byte response{};
    bool idle = false;
    for (unsigned int attempt = 0; attempt < reset_attempts && !idle; ++attempt) {
        status = command(0, 0, response);
        idle = status == mm::fs::Status::Ok && response == in_idle;
    }
    if (!idle) return forget(mm::fs::Status::TransportError);

    // CMD8 answers only on a version 2 card; a version 1 card calls it
    // illegal, and is standard capacity.
    status = command(8, 0x1aa, response);
    if (status != mm::fs::Status::Ok) return forget(status);
    const bool version_2 = (response & illegal_command) == std::byte{0};
    if (version_2) {
        std::array<std::byte, 4> echo{};
        status = receive(echo);
        if (status != mm::fs::Status::Ok) return forget(status);
        if ((bits(echo, 2) & 0x0fu) != 1u || bits(echo, 3) != 0xaau)
            return forget(mm::fs::Status::Unsupported);
    }

    unsigned long start = 0;
    static_cast<void>(mm::mcu::ticks_ms(start));
    while (true) {
        status = application_command(41, version_2 ? 0x4000'0000u : 0u, response);
        if (status != mm::fs::Status::Ok) return forget(status);
        if (response == std::byte{0}) break;
        if (response != in_idle) return forget(mm::fs::Status::Unsupported);   // not SD
        if (expired(start, idle_timeout_ms)) return forget(mm::fs::Status::Timeout);
        static_cast<void>(mm::mcu::delay_ms(1));
    }

    block_addressed_ = false;
    if (version_2) {
        status = command(58, 0, response);
        if (status != mm::fs::Status::Ok || response != std::byte{0})
            return forget(mm::fs::Status::TransportError);
        std::array<std::byte, 4> ocr{};
        status = receive(ocr);
        if (status != mm::fs::Status::Ok) return forget(status);
        block_addressed_ = (bits(ocr, 0) & 0x40u) != 0;
    }

    status = command(59, 1, response);    // CRC checking on
    if (status != mm::fs::Status::Ok || response != std::byte{0})
        return forget(mm::fs::Status::TransportError);
    if (!block_addressed_) {
        status = command(16, block_size, response);
        if (status != mm::fs::Status::Ok || response != std::byte{0})
            return forget(mm::fs::Status::TransportError);
    }

    status = command(9, 0, response);
    if (status != mm::fs::Status::Ok || response != std::byte{0})
        return forget(mm::fs::Status::TransportError);
    std::array<std::byte, 16> csd{};
    status = receive_block(csd);
    if (status != mm::fs::Status::Ok) return forget(status);
    if (!csd_block_count(csd, block_count_)) return forget(mm::fs::Status::Unsupported);

    status = finish(mm::fs::Status::Ok);
    if (status != mm::fs::Status::Ok) return forget(status);
    status = configure(wiring_.data_baud);
    if (status != mm::fs::Status::Ok) return status;
    ready_ = true;
    return mm::fs::Status::Ok;
}

mm::fs::Status SpiCard::ready() { return ready_ ? mm::fs::Status::Ok : initialize(); }

mm::fs::Status SpiCard::geometry(mm::fs::BlockGeometry& geometry) {
    const auto status = ready();
    if (status == mm::fs::Status::Ok) geometry = {block_count_, block_size};
    return status;
}

mm::fs::Status SpiCard::read(std::uint64_t block, std::span<std::byte> data) {
    if (data.empty() || data.size() % block_size != 0) return mm::fs::Status::BadArgument;
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    const std::uint64_t count = data.size() / block_size;
    if (block > block_count_ || count > block_count_ - block) return mm::fs::Status::BadArgument;
    const auto address = static_cast<std::uint32_t>(block_addressed_ ? block : block * block_size);

    status = select(true);
    if (status != mm::fs::Status::Ok) return forget(status);
    std::byte response{};
    status = command(count == 1 ? 17 : 18, address, response);
    if (status != mm::fs::Status::Ok || response != std::byte{0})
        return forget(status != mm::fs::Status::Ok ? status : mm::fs::Status::TransportError);
    for (std::uint64_t i = 0; i < count; ++i) {
        status = receive_block(data.subspan(static_cast<std::size_t>(i) * block_size, block_size));
        if (status != mm::fs::Status::Ok) return forget(status);
    }
    if (count > 1) {
        status = command(12, 0, response);
        if (status != mm::fs::Status::Ok) return forget(status);
        status = wait_ready(busy_timeout_ms);
        if (status != mm::fs::Status::Ok) return forget(status);
    }
    return finish(mm::fs::Status::Ok);
}

mm::fs::Status SpiCard::write(std::uint64_t block, std::span<const std::byte> data) {
    if (data.empty() || data.size() % block_size != 0) return mm::fs::Status::BadArgument;
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    const std::uint64_t count = data.size() / block_size;
    if (block > block_count_ || count > block_count_ - block) return mm::fs::Status::BadArgument;
    const auto address = static_cast<std::uint32_t>(block_addressed_ ? block : block * block_size);

    status = select(true);
    if (status != mm::fs::Status::Ok) return forget(status);
    std::byte response{};
    status = command(count == 1 ? 24 : 25, address, response);
    if (status != mm::fs::Status::Ok || response != std::byte{0})
        return forget(status != mm::fs::Status::Ok ? status : mm::fs::Status::TransportError);
    for (std::uint64_t i = 0; i < count; ++i) {
        status = send_block(count == 1 ? start_block : start_multiple,
                            data.subspan(static_cast<std::size_t>(i) * block_size, block_size));
        if (status != mm::fs::Status::Ok) return forget(status);
    }
    if (count > 1) {
        std::byte ignored{};
        status = byte(stop_multiple, ignored);
        if (status == mm::fs::Status::Ok) status = byte(idle_byte, ignored);
        if (status == mm::fs::Status::Ok) status = wait_ready(busy_timeout_ms);
        if (status != mm::fs::Status::Ok) return forget(status);
    }
    // CMD13 reports a write the card accepted and then failed to program.
    status = command(13, 0, response);
    std::byte second{};
    if (status == mm::fs::Status::Ok) status = byte(idle_byte, second);
    if (status != mm::fs::Status::Ok) return forget(status);
    if (response != std::byte{0} || second != std::byte{0})
        return forget(mm::fs::Status::TransportError);
    return finish(mm::fs::Status::Ok);
}

mm::fs::Status SpiCard::sync() { return mm::fs::Status::Ok; }

}  // namespace mm::sdcard
