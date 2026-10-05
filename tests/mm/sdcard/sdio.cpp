// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// SdioCard against a model SD card in SD mode behind a fake mm.mcu platform
// whose sdio facility answers at the level the facility promises: a command
// and its response, whole blocks after a read or write command. The model
// keeps the card's state -- idle, ready, identified, standby, transfer -- its
// RCA, its bus width, and its blocks, and can be failed on purpose.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.test;

namespace {

using mm::fs::Status;
using mm::test::expect;

enum class Kind { Sdhc, SdscVersion2, SdscVersion1 };
enum class State { Idle, Ready, Identified, Standby, Transfer };

constexpr std::uint32_t illegal_command = 1u << 22;
constexpr std::uint32_t address_error = 1u << 30;
constexpr std::uint32_t rca = 0x1234;

class SdioPlatform final : public mm::mcu::Platform {
public:
    Kind kind = Kind::Sdhc;
    bool present = true;
    bool never_ready = false;
    bool corrupt_next_read = false;
    bool reject_next_write = false;
    std::vector<std::uint8_t> storage = std::vector<std::uint8_t>(4096u * 512u, 0);
    std::vector<unsigned int> commands;
    std::vector<std::uint32_t> arguments;
    std::vector<unsigned long> clocks;
    std::vector<std::size_t> read_runs;
    mm::mcu::SdioConfiguration configured{};
    unsigned int configurations = 0;
    unsigned int width = 1;
    unsigned long idle = 0;

    explicit SdioPlatform(Kind card_kind) : kind(card_kind) {}

    [[nodiscard]] std::size_t count(unsigned int index) const {
        return static_cast<std::size_t>(std::count(commands.begin(), commands.end(), index));
    }

    [[nodiscard]] mm::mcu::Status sdio_configure(
        const mm::mcu::SdioConfiguration& configuration) override {
        configured = configuration;
        ++configurations;
        return mm::mcu::Status::Ok;
    }
    [[nodiscard]] mm::mcu::Status sdio_clock(unsigned int, unsigned long hz,
                                             unsigned long& actual) override {
        clocks.push_back(hz);
        actual = hz;
        return mm::mcu::Status::Ok;
    }
    [[nodiscard]] mm::mcu::Status sdio_idle_clocks(unsigned int, unsigned int count) override {
        idle += count;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status sdio_command(unsigned int, unsigned int index,
                                               std::uint32_t argument,
                                               mm::mcu::SdioResponse response,
                                               std::span<std::uint32_t> words) override {
        commands.push_back(index);
        arguments.push_back(argument);
        // Nothing answers in an empty socket, which a command expecting no
        // answer cannot tell.
        if (!present)
            return response == mm::mcu::SdioResponse::None ? mm::mcu::Status::Ok
                                                           : mm::mcu::Status::Timeout;
        const bool application = application_;
        application_ = false;
        const auto status_word = [&](std::uint32_t extra) {
            return extra | (static_cast<std::uint32_t>(state_number()) << 9);
        };
        switch (index) {
            case 0:
                state_ = State::Idle;
                width = 1;
                polls_ = 0;
                return expect_kind(response, mm::mcu::SdioResponse::None);
            case 8:
                if (kind == Kind::SdscVersion1) return mm::mcu::Status::Timeout;
                words[0] = argument & 0xfffu;
                return expect_kind(response, mm::mcu::SdioResponse::Short);
            case 55:
                if (state_ != State::Idle && state_ != State::Ready &&
                    (argument >> 16) != rca)
                    return mm::mcu::Status::Timeout;    // addressed to another card
                application_ = true;
                words[0] = status_word(1u << 5);
                return expect_kind(response, mm::mcu::SdioResponse::Short);
            case 41: {
                if (!application) return mm::mcu::Status::Timeout;
                const bool asks_high = (argument & 0x4000'0000u) != 0;
                if ((argument & 0x00ff'8000u) == 0) {
                    words[0] = 0;    // no voltage asked: the card stays idle
                    return expect_kind(response, mm::mcu::SdioResponse::ShortNoCrc);
                }
                const bool done = !never_ready && ++polls_ >= 3 &&
                                  (kind != Kind::Sdhc || asks_high);
                words[0] = 0x00ff'8000u | (done ? 0x8000'0000u : 0u) |
                           (done && kind == Kind::Sdhc ? 0x4000'0000u : 0u);
                if (done) state_ = State::Ready;
                return expect_kind(response, mm::mcu::SdioResponse::ShortNoCrc);
            }
            case 2:
                if (state_ != State::Ready) return mm::mcu::Status::Timeout;
                state_ = State::Identified;
                std::fill(words.begin(), words.end(), 0x6d6d4d4du);
                return expect_kind(response, mm::mcu::SdioResponse::Long);
            case 3:
                if (state_ != State::Identified) return mm::mcu::Status::Timeout;
                state_ = State::Standby;
                words[0] = rca << 16;
                return expect_kind(response, mm::mcu::SdioResponse::Short);
            case 9: {
                if (state_ != State::Standby || (argument >> 16) != rca)
                    return mm::mcu::Status::Timeout;
                const auto csd = csd_bytes();
                for (std::size_t i = 0; i < 4; ++i)
                    words[i] = (std::uint32_t{csd[4 * i]} << 24) |
                               (std::uint32_t{csd[4 * i + 1]} << 16) |
                               (std::uint32_t{csd[4 * i + 2]} << 8) | csd[4 * i + 3];
                return expect_kind(response, mm::mcu::SdioResponse::Long);
            }
            case 7:
                if ((argument >> 16) != rca) return mm::mcu::Status::Timeout;
                state_ = State::Transfer;
                words[0] = status_word(0);
                return expect_kind(response, mm::mcu::SdioResponse::ShortBusy);
            case 6:
                if (!application || state_ != State::Transfer) {
                    words[0] = status_word(illegal_command);
                    return mm::mcu::Status::Ok;
                }
                width = argument == 2 ? 4u : 1u;
                words[0] = status_word(0);
                return expect_kind(response, mm::mcu::SdioResponse::Short);
            case 16:
                words[0] = status_word(argument == 512 ? 0u : (1u << 29));
                return expect_kind(response, mm::mcu::SdioResponse::Short);
            case 12:
                words[0] = status_word(0);
                return expect_kind(response, mm::mcu::SdioResponse::ShortBusy);
            case 13:
                words[0] = status_word(0);
                return expect_kind(response, mm::mcu::SdioResponse::Short);
            default:
                words[0] = status_word(illegal_command);
                return mm::mcu::Status::Ok;
        }
    }

    [[nodiscard]] mm::mcu::Status sdio_read(unsigned int, unsigned int index,
                                            std::uint32_t argument, std::uint32_t& response,
                                            std::span<std::byte> data,
                                            unsigned int block_size) override {
        commands.push_back(index);
        arguments.push_back(argument);
        if (!present) return mm::mcu::Status::Timeout;
        if (block_size != 512 || data.size() % 512 != 0 ||
            data.size() / 512 > mm::mcu::sdio_read_blocks_at_least)
            return mm::mcu::Status::BadArgument;
        if (state_ != State::Transfer || width != 4 || (index != 17 && index != 18) ||
            (index == 17) != (data.size() == 512))
            return mm::mcu::Status::Timeout;
        std::size_t first = 0;
        if (!address(argument, data.size(), first)) {
            response = address_error;
            return mm::mcu::Status::TransportError;
        }
        read_runs.push_back(data.size() / 512);
        response = static_cast<std::uint32_t>(state_number()) << 9;
        std::copy_n(storage.begin() + static_cast<std::ptrdiff_t>(first), data.size(),
                    reinterpret_cast<std::uint8_t*>(data.data()));
        if (corrupt_next_read) {
            corrupt_next_read = false;
            return mm::mcu::Status::TransportError;    // a CRC16 that did not match
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status sdio_write(unsigned int, unsigned int index,
                                             std::uint32_t argument, std::uint32_t& response,
                                             std::span<const std::byte> data,
                                             unsigned int block_size) override {
        commands.push_back(index);
        arguments.push_back(argument);
        if (!present) return mm::mcu::Status::Timeout;
        if (block_size != 512 || data.size() % 512 != 0) return mm::mcu::Status::BadArgument;
        if (state_ != State::Transfer || width != 4 || (index != 24 && index != 25) ||
            (index == 24) != (data.size() == 512))
            return mm::mcu::Status::Timeout;
        std::size_t first = 0;
        if (!address(argument, data.size(), first)) {
            response = address_error;
            return mm::mcu::Status::TransportError;
        }
        response = static_cast<std::uint32_t>(state_number()) << 9;
        if (reject_next_write) {
            reject_next_write = false;
            return mm::mcu::Status::TransportError;    // the CRC status said write error
        }
        std::copy_n(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(),
                    storage.begin() + static_cast<std::ptrdiff_t>(first));
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = now_;
        return mm::mcu::Status::Ok;
    }
    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        now_ += milliseconds;
        return mm::mcu::Status::Ok;
    }

private:
    [[nodiscard]] int state_number() const {
        switch (state_) {
            case State::Idle: return 0;
            case State::Ready: return 1;
            case State::Identified: return 2;
            case State::Standby: return 3;
            case State::Transfer: return 4;
        }
        return 0;
    }

    static mm::mcu::Status expect_kind(mm::mcu::SdioResponse got, mm::mcu::SdioResponse wanted) {
        return got == wanted ? mm::mcu::Status::Ok : mm::mcu::Status::TransportError;
    }

    [[nodiscard]] bool address(std::uint32_t argument, std::size_t size, std::size_t& first) const {
        const std::uint64_t byte =
            kind == Kind::Sdhc ? std::uint64_t{argument} * 512 : std::uint64_t{argument};
        if (kind != Kind::Sdhc && argument % 512 != 0) return false;
        if (byte > storage.size() || size > storage.size() - byte) return false;
        first = static_cast<std::size_t>(byte);
        return true;
    }

    [[nodiscard]] std::array<std::uint8_t, 16> csd_bytes() const {
        std::array<std::uint8_t, 16> csd{};
        const std::uint64_t blocks = storage.size() / 512;
        if (kind == Kind::Sdhc) {
            const std::uint64_t size = blocks / 1024 - 1;
            csd[0] = 0x40;
            csd[7] = static_cast<std::uint8_t>((size >> 16) & 0x3f);
            csd[8] = static_cast<std::uint8_t>(size >> 8);
            csd[9] = static_cast<std::uint8_t>(size);
        } else {
            const std::uint64_t size = blocks / 512 - 1;    // READ_BL_LEN 9, C_SIZE_MULT 7
            csd[5] = 0x09;
            csd[6] = static_cast<std::uint8_t>((size >> 10) & 0x03);
            csd[7] = static_cast<std::uint8_t>(size >> 2);
            csd[8] = static_cast<std::uint8_t>((size & 0x03) << 6);
            csd[9] = 0x03;
            csd[10] = 0x80;
        }
        csd[15] = 0x01;
        return csd;
    }

    State state_ = State::Idle;
    bool application_ = false;
    unsigned int polls_ = 0;
    unsigned long now_ = 0;
};

class Selected {
public:
    explicit Selected(mm::mcu::Platform& platform) : saved_(mm::mcu::platform()) {
        mm::mcu::set_platform(platform);
    }
    ~Selected() { mm::mcu::set_platform(saved_); }
    Selected(const Selected&) = delete;
    Selected& operator=(const Selected&) = delete;

private:
    mm::mcu::Platform& saved_;
};

mm::sdcard::SdioWiring wiring() {
    return {.instance = 0, .clock_gpio = 19, .command_gpio = 20, .data0_gpio = 21,
            .data_clock_hz = 25'000'000};
}

std::vector<std::byte> pattern(std::size_t size, unsigned int seed) {
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::byte>((i * 13 + seed * 7 + 1) & 0xffu);
    return bytes;
}

void an_sdhc_card_is_identified_in_sd_mode() {
    SdioPlatform platform{Kind::Sdhc};
    const Selected selected{platform};
    mm::sdcard::SdioCard card{wiring()};
    expect(platform.commands.empty() && platform.configurations == 0,
           "nothing happens at construction");
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::Ok && geometry.count == 4096 &&
               geometry.size == 512,
           "geometry comes from the CSD in CMD9's R2");
    expect(platform.configured.clock_gpio == 19 && platform.configured.command_gpio == 20 &&
               platform.configured.data0_gpio == 21 && platform.configured.width == 4,
           "the bus is configured on the wiring's pins, four data lines");
    expect(platform.idle >= 74, "at least 74 clocks before the first command");
    const std::vector<unsigned int> expected{0, 8, 55, 41, 55, 41, 55, 41, 2, 3, 9, 7, 55, 6};
    expect(platform.commands == expected,
           "CMD0, CMD8, ACMD41 until ready, CMD2, CMD3, CMD9, CMD7, ACMD6 -- and no CMD16");
    expect(platform.arguments[1] == 0x1aa && (platform.arguments[3] & 0x40ff8000u) == 0x40ff8000u,
           "CMD8 carries the check pattern and ACMD41 HCS and the voltage window");
    expect(platform.arguments[10] == (rca << 16) && platform.arguments[11] == (rca << 16) &&
               platform.arguments[12] == (rca << 16) && platform.arguments[13] == 2,
           "CMD9, CMD7, and CMD55 are addressed by the RCA, and ACMD6 asks for four bits");
    expect(platform.clocks.size() == 2 && platform.clocks[0] == 400'000 &&
               platform.clocks[1] == 25'000'000 && card.data_clock_hz() == 25'000'000,
           "identification at 400 kHz, then the data clock");
    expect(card.geometry(geometry) == Status::Ok && platform.count(0) == 1,
           "and the card is not identified twice");
}

void blocks_round_trip_in_four_bit_mode() {
    SdioPlatform platform{Kind::Sdhc};
    const Selected selected{platform};
    mm::sdcard::SdioCard card{wiring()};
    const auto one = pattern(512, 1);
    expect(card.write(9, one) == Status::Ok, "a block writes with CMD24");
    expect(std::equal(one.begin(), one.end(),
                      reinterpret_cast<const std::byte*>(platform.storage.data()) + 9 * 512),
           "at block 9: block addressing");
    expect(platform.count(24) == 1 && platform.count(13) == 1 && platform.count(12) == 0,
           "CMD24, then CMD13 to confirm, and no CMD12");
    std::vector<std::byte> back(512);
    expect(card.read(9, back) == Status::Ok && back == one, "and reads back with CMD17");

    const auto many = pattern(20 * 512, 2);
    expect(card.write(100, many) == Status::Ok, "twenty blocks write with CMD25");
    expect(platform.count(25) == 1 && platform.count(12) == 1, "one CMD25, stopped by CMD12");
    std::vector<std::byte> lots(20 * 512);
    platform.read_runs.clear();
    expect(card.read(100, lots) == Status::Ok && lots == many, "and read back");
    expect(platform.read_runs == std::vector<std::size_t>{8, 8, 4} && platform.count(18) == 3 &&
               platform.count(12) == 4,
           "in reads of at most eight blocks, each a CMD18 stopped by CMD12");
}

void standard_capacity_cards_are_byte_addressed() {
    for (const auto kind : {Kind::SdscVersion2, Kind::SdscVersion1}) {
        SdioPlatform platform{kind};
        const Selected selected{platform};
        mm::sdcard::SdioCard card{wiring()};
        mm::fs::BlockGeometry geometry;
        expect(card.geometry(geometry) == Status::Ok && geometry.count == 4096,
               "a version 1 CSD gives the capacity");
        expect(platform.count(16) == 1, "a standard-capacity card is given CMD16");
        if (kind == Kind::SdscVersion1)
            expect((platform.arguments[3] & 0x4000'0000u) == 0,
                   "a card silent to CMD8 is not asked for high capacity");
        const auto data = pattern(1024, 3);
        expect(card.write(5, data) == Status::Ok && platform.arguments.back() == (rca << 16),
               "two blocks write");
        expect(std::equal(data.begin(), data.end(),
                          reinterpret_cast<const std::byte*>(platform.storage.data()) + 5 * 512),
               "byte address 2560 is block 5");
        std::vector<std::byte> back(1024);
        expect(card.read(5, back) == Status::Ok && back == data, "and read back");
    }
}

void arguments_are_checked_before_the_bus() {
    SdioPlatform platform{Kind::Sdhc};
    const Selected selected{platform};
    mm::sdcard::SdioCard card{wiring()};
    std::vector<std::byte> odd(100);
    std::vector<std::byte> block(512);
    expect(card.read(0, odd) == Status::BadArgument && card.write(0, odd) == Status::BadArgument,
           "partial blocks are refused");
    expect(card.read(0, {}) == Status::BadArgument, "an empty read is refused");
    expect(platform.commands.empty(), "without touching the bus");
    expect(card.read(4096, block) == Status::BadArgument, "past the end is refused");
}

void a_missing_card_is_a_transport_error() {
    SdioPlatform platform{Kind::Sdhc};
    platform.present = false;
    const Selected selected{platform};
    mm::sdcard::SdioCard card{wiring()};
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::TransportError, "an empty socket");
    platform.present = true;
    expect(card.geometry(geometry) == Status::Ok, "a card inserted later is found");
}

void a_card_that_never_leaves_busy_times_out() {
    SdioPlatform platform{Kind::Sdhc};
    platform.never_ready = true;
    const Selected selected{platform};
    mm::sdcard::SdioCard card{wiring()};
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::Timeout, "ACMD41 never reports ready");
}

void failures_forget_the_card() {
    SdioPlatform platform{Kind::Sdhc};
    const Selected selected{platform};
    mm::sdcard::SdioCard card{wiring()};
    std::vector<std::byte> block(512);
    platform.corrupt_next_read = true;
    expect(card.read(3, block) == Status::TransportError, "a bad data CRC is TransportError");
    expect(card.read(3, block) == Status::Ok && platform.count(0) == 2,
           "and the next call identifies the card again");
    platform.reject_next_write = true;
    expect(card.write(3, block) == Status::TransportError, "a rejected block is TransportError");
    expect(card.write(3, block) == Status::Ok && platform.count(0) == 3, "and recovers");
}

void a_platform_without_sdio_is_unsupported() {
    mm::mcu::Platform bare;
    const Selected selected{bare};
    mm::sdcard::SdioCard card{wiring()};
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::Unsupported, "no sdio facility");
}

void the_csd_gives_the_capacity() {
    std::array<std::byte, 16> v2{};
    v2[0] = std::byte{0x40};
    v2[8] = std::byte{0x3b};
    v2[9] = std::byte{0x37};    // C_SIZE 15159: a 7.4 GiB card
    std::uint64_t blocks = 0;
    expect(mm::sdcard::csd_block_count(v2, blocks) && blocks == 15160ull * 1024,
           "version 2: (C_SIZE + 1) * 1024 blocks");
    std::array<std::byte, 16> other{};
    other[0] = std::byte{0x80};
    expect(!mm::sdcard::csd_block_count(other, blocks), "an unknown structure is refused");
}

const mm::test::case_ cases[] = {
    {"an SDHC card is identified in SD mode", &an_sdhc_card_is_identified_in_sd_mode},
    {"blocks round trip in 4-bit mode", &blocks_round_trip_in_four_bit_mode},
    {"standard-capacity cards are byte addressed over SDIO",
     &standard_capacity_cards_are_byte_addressed},
    {"SDIO arguments are checked before the bus", &arguments_are_checked_before_the_bus},
    {"a missing SDIO card is a transport error", &a_missing_card_is_a_transport_error},
    {"an SDIO card that never leaves busy times out", &a_card_that_never_leaves_busy_times_out},
    {"SDIO failures forget the card", &failures_forget_the_card},
    {"a platform without sdio is Unsupported", &a_platform_without_sdio_is_unsupported},
    {"the CSD gives the capacity", &the_csd_gives_the_capacity},
};

const mm::test::registrar reg{"mm.sdcard SdioCard", cases};

}  // namespace
