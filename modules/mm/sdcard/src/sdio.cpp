// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// An SD card on its native bus, over mm.mcu's sdio facility: the card
// protocol of the SD Physical Layer Simplified Specification's SD mode. The
// platform frames commands, checks responses and data CRCs, and waits out
// busy; this is identification, addressing, and the command sequences.
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
constexpr unsigned long identification_hz = 400'000;
constexpr unsigned long ready_timeout_ms = 1'000;
constexpr std::size_t most_blocks = mm::mcu::sdio_read_blocks_at_least;

// R1's error bits: OUT_OF_RANGE through ERROR, CARD_IS_LOCKED excepted, and
// AKE_SEQ_ERROR.
constexpr std::uint32_t status_errors = 0xfdf80008u;
// OCR: power-up done, card capacity status, and the 2.7 V to 3.6 V window.
constexpr std::uint32_t ocr_ready = 0x8000'0000u;
constexpr std::uint32_t ocr_high_capacity = 0x4000'0000u;
constexpr std::uint32_t ocr_voltages = 0x00ff'8000u;

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

SdioCard::SdioCard(SdioWiring wiring) : wiring_(wiring) {}

bool SdioCard::expired(unsigned long start, unsigned long timeout_ms) const {
    unsigned long now = 0;
    if (mm::mcu::ticks_ms(now) != mm::mcu::Status::Ok) return true;
    return now - start >= timeout_ms;
}

mm::fs::Status SdioCard::forget(mm::fs::Status status) {
    ready_ = false;
    clock_hz_ = 0;
    return status;
}

mm::fs::Status SdioCard::command(unsigned int index, std::uint32_t argument,
                                 mm::mcu::SdioResponse response,
                                 std::span<std::uint32_t> words) {
    return from(mm::mcu::sdio_command(wiring_.instance, index, argument, response, words));
}

// A command whose R1 or R1b status must show no error.
mm::fs::Status SdioCard::status_command(unsigned int index, std::uint32_t argument,
                                        mm::mcu::SdioResponse response) {
    std::array<std::uint32_t, 1> word{};
    const auto status = command(index, argument, response, word);
    if (status != mm::fs::Status::Ok) return status;
    return (word[0] & status_errors) == 0 ? mm::fs::Status::Ok : mm::fs::Status::TransportError;
}

// CMD55, addressed to the card once it has an RCA, then the application
// command.
mm::fs::Status SdioCard::application_command(unsigned int index, std::uint32_t argument,
                                             mm::mcu::SdioResponse response,
                                             std::uint32_t& word) {
    auto status = status_command(55, rca_ << 16, mm::mcu::SdioResponse::Short);
    if (status != mm::fs::Status::Ok) return status;
    std::array<std::uint32_t, 1> words{};
    status = command(index, argument, response, words);
    if (status == mm::fs::Status::Ok) word = words[0];
    return status;
}

mm::fs::Status SdioCard::initialize() {
    ready_ = false;
    clock_hz_ = 0;
    rca_ = 0;
    auto status = from(mm::mcu::sdio_configure({.instance = wiring_.instance,
                                                .clock_gpio = wiring_.clock_gpio,
                                                .command_gpio = wiring_.command_gpio,
                                                .data0_gpio = wiring_.data0_gpio,
                                                .width = 4}));
    if (status != mm::fs::Status::Ok) return status;
    unsigned long actual = 0;
    status = from(mm::mcu::sdio_clock(wiring_.instance, identification_hz, actual));
    if (status != mm::fs::Status::Ok) return status;
    status = from(mm::mcu::sdio_idle_clocks(wiring_.instance, 80));
    if (status != mm::fs::Status::Ok) return status;

    status = command(0, 0, mm::mcu::SdioResponse::None, {});
    if (status != mm::fs::Status::Ok) return forget(status);

    // CMD8 answers only on a version 2 card; a version 1 card stays silent,
    // and is standard capacity.
    std::array<std::uint32_t, 1> word{};
    status = command(8, 0x1aa, mm::mcu::SdioResponse::Short, word);
    bool version_2 = false;
    if (status == mm::fs::Status::Ok) {
        if ((word[0] & 0xfffu) != 0x1aau) return forget(mm::fs::Status::Unsupported);
        version_2 = true;
    } else if (status != mm::fs::Status::Timeout) {
        return forget(status);
    }

    unsigned long start = 0;
    static_cast<void>(mm::mcu::ticks_ms(start));
    std::uint32_t ocr = 0;
    while (true) {
        status = application_command(
            41, ocr_voltages | (version_2 ? ocr_high_capacity : 0u),
            mm::mcu::SdioResponse::ShortNoCrc, ocr);
        // Silence to the first CMD55 is an empty socket.
        if (status == mm::fs::Status::Timeout) return forget(mm::fs::Status::TransportError);
        if (status != mm::fs::Status::Ok) return forget(status);
        if ((ocr & ocr_ready) != 0) break;
        if ((ocr & ocr_voltages) == 0) return forget(mm::fs::Status::Unsupported);
        if (expired(start, ready_timeout_ms)) return forget(mm::fs::Status::Timeout);
        static_cast<void>(mm::mcu::delay_ms(1));
    }
    block_addressed_ = version_2 && (ocr & ocr_high_capacity) != 0;

    std::array<std::uint32_t, 4> long_words{};
    status = command(2, 0, mm::mcu::SdioResponse::Long, long_words);    // the CID
    if (status != mm::fs::Status::Ok) return forget(status);
    status = command(3, 0, mm::mcu::SdioResponse::Short, word);         // an RCA
    if (status != mm::fs::Status::Ok) return forget(status);
    rca_ = word[0] >> 16;
    status = command(9, rca_ << 16, mm::mcu::SdioResponse::Long, long_words);    // the CSD
    if (status != mm::fs::Status::Ok) return forget(status);
    std::array<std::byte, 16> csd{};
    for (std::size_t i = 0; i < csd.size(); ++i)
        csd[i] = static_cast<std::byte>(long_words[i / 4] >> (24 - 8 * (i % 4)));
    if (!csd_block_count(csd, block_count_)) return forget(mm::fs::Status::Unsupported);

    status = status_command(7, rca_ << 16, mm::mcu::SdioResponse::ShortBusy);    // select
    if (status != mm::fs::Status::Ok) return forget(status);
    std::uint32_t ignored = 0;
    status = application_command(6, 2, mm::mcu::SdioResponse::Short, ignored);    // 4-bit
    if (status == mm::fs::Status::Ok && (ignored & status_errors) != 0)
        status = mm::fs::Status::TransportError;
    if (status != mm::fs::Status::Ok) return forget(status);
    if (!block_addressed_) {
        status = status_command(16, block_size, mm::mcu::SdioResponse::Short);
        if (status != mm::fs::Status::Ok) return forget(status);
    }

    status = from(mm::mcu::sdio_clock(wiring_.instance, wiring_.data_clock_hz, actual));
    if (status != mm::fs::Status::Ok) return forget(status);
    clock_hz_ = actual;
    ready_ = true;
    return mm::fs::Status::Ok;
}

mm::fs::Status SdioCard::ready() { return ready_ ? mm::fs::Status::Ok : initialize(); }

mm::fs::Status SdioCard::geometry(mm::fs::BlockGeometry& geometry) {
    const auto status = ready();
    if (status == mm::fs::Status::Ok) geometry = {block_count_, block_size};
    return status;
}

mm::fs::Status SdioCard::read(std::uint64_t block, std::span<std::byte> data) {
    if (data.empty() || data.size() % block_size != 0) return mm::fs::Status::BadArgument;
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    const std::uint64_t count = data.size() / block_size;
    if (block > block_count_ || count > block_count_ - block) return mm::fs::Status::BadArgument;

    // At most the platform's guaranteed run a command; CMD12 ends each
    // multiple read.
    while (!data.empty()) {
        const auto blocks = std::min<std::size_t>(data.size() / block_size, most_blocks);
        const auto address =
            static_cast<std::uint32_t>(block_addressed_ ? block : block * block_size);
        std::uint32_t response = 0;
        status = from(mm::mcu::sdio_read(wiring_.instance, blocks == 1 ? 17 : 18, address,
                                         response, data.first(blocks * block_size),
                                         block_size));
        if (status == mm::fs::Status::Ok && (response & status_errors) != 0)
            status = mm::fs::Status::TransportError;
        if (blocks > 1) {
            const auto stopped = status_command(12, 0, mm::mcu::SdioResponse::ShortBusy);
            if (status == mm::fs::Status::Ok) status = stopped;
        }
        if (status != mm::fs::Status::Ok) return forget(status);
        data = data.subspan(blocks * block_size);
        block += blocks;
    }
    return mm::fs::Status::Ok;
}

mm::fs::Status SdioCard::write(std::uint64_t block, std::span<const std::byte> data) {
    if (data.empty() || data.size() % block_size != 0) return mm::fs::Status::BadArgument;
    auto status = ready();
    if (status != mm::fs::Status::Ok) return status;
    const std::uint64_t count = data.size() / block_size;
    if (block > block_count_ || count > block_count_ - block) return mm::fs::Status::BadArgument;
    const auto address = static_cast<std::uint32_t>(block_addressed_ ? block : block * block_size);

    std::uint32_t response = 0;
    status = from(mm::mcu::sdio_write(wiring_.instance, count == 1 ? 24 : 25, address, response,
                                      data, block_size));
    if (status == mm::fs::Status::Ok && (response & status_errors) != 0)
        status = mm::fs::Status::TransportError;
    if (count > 1) {
        const auto stopped = status_command(12, 0, mm::mcu::SdioResponse::ShortBusy);
        if (status == mm::fs::Status::Ok) status = stopped;
    }
    if (status != mm::fs::Status::Ok) return forget(status);
    // CMD13 reports a write the card accepted and then failed to program.
    status = status_command(13, rca_ << 16, mm::mcu::SdioResponse::Short);
    return status == mm::fs::Status::Ok ? status : forget(status);
}

mm::fs::Status SdioCard::sync() { return mm::fs::Status::Ok; }

}  // namespace mm::sdcard
