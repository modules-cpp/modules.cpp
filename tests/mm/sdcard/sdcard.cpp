// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.sdcard.socket;
import mm.test;

void mm_test_sdcard_reset(int kind, std::size_t blocks);
void mm_test_sdcard_present(bool present);
void mm_test_sdcard_never_ready(bool value);
void mm_test_sdcard_corrupt_next_read();
void mm_test_sdcard_pull_after(long bytes);
std::size_t mm_test_sdcard_commands();
unsigned int mm_test_sdcard_command(std::size_t i);
std::uint32_t mm_test_sdcard_argument(std::size_t i);
std::size_t mm_test_sdcard_count(unsigned int index);
std::uint8_t mm_test_sdcard_storage(std::size_t offset);
void mm_test_sdcard_store(std::size_t offset, std::uint8_t value);
std::vector<unsigned long> mm_test_sdcard_bauds();
bool mm_test_sdcard_deselected();
unsigned long mm_test_sdcard_clocks_deselected();

namespace {

using mm::fs::Status;
using mm::test::expect;

constexpr int sdhc = 0;
constexpr int sdsc_version_2 = 1;
constexpr int sdsc_version_1 = 2;

mm::sdcard::SpiWiring wiring() {
    return {.spi = {.instance = 0, .clock_gpio = 18, .transmit_gpio = 19, .receive_gpio = 20},
            .chip_select_gpio = 23,
            .data_baud = 25'000'000};
}

[[nodiscard]] bool sent(unsigned int index) { return mm_test_sdcard_count(index) > 0; }

void the_crcs_match_the_specification() {
    const std::array<std::byte, 5> cmd0{std::byte{0x40}, std::byte{0}, std::byte{0}, std::byte{0},
                                        std::byte{0}};
    const std::array<std::byte, 5> cmd8{std::byte{0x48}, std::byte{0}, std::byte{0},
                                        std::byte{0x01}, std::byte{0xaa}};
    expect(((mm::sdcard::crc7(cmd0) << 1) | 1) == 0x95, "CMD0's CRC byte is 0x95");
    expect(((mm::sdcard::crc7(cmd8) << 1) | 1) == 0x87, "CMD8's is 0x87");
    std::array<std::byte, 512> ones{};
    for (auto& b : ones) b = std::byte{0xff};
    expect(mm::sdcard::crc16(ones) == 0x7fa1, "512 bytes of 0xFF have CRC16 0x7FA1");
}

void an_sdhc_card_initializes() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm::sdcard::SpiCard card{wiring()};
    expect(mm_test_sdcard_commands() == 0, "nothing happens at construction");
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::Ok && geometry.count == 2048 &&
               geometry.size == 512,
           "geometry comes from the CSD");
    expect(mm_test_sdcard_command(0) == 0 && mm_test_sdcard_command(1) == 8 &&
               mm_test_sdcard_argument(1) == 0x1aa,
           "CMD0, then CMD8 with the check pattern");
    expect(sent(55) && sent(41) && sent(58) && sent(59) && sent(9) && !sent(16),
           "ACMD41, CMD58, CMD59, and CMD9, and no CMD16 on a block-addressed card");
    const auto bauds = mm_test_sdcard_bauds();
    expect(bauds.size() == 2 && bauds[0] == 400'000 && bauds[1] == 25'000'000,
           "identification at 400 kHz, then the data clock");
    expect(mm_test_sdcard_deselected(), "chip select is high afterwards");
    expect(card.geometry(geometry) == Status::Ok && mm_test_sdcard_count(0) == 1,
           "and the card is not initialised twice");
}

void blocks_round_trip_on_sdhc() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm::sdcard::SpiCard card{wiring()};
    std::vector<std::byte> out(3 * 512);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>(i * 7);
    expect(card.write(10, std::span{out}.first(512)) == Status::Ok, "a single block writes");
    expect(mm_test_sdcard_storage(10 * 512 + 1) == 7, "and lands at block 10");
    expect(card.write(20, out) == Status::Ok && sent(25), "three blocks write with CMD25");
    expect(mm_test_sdcard_storage(22 * 512 + 3) == static_cast<std::uint8_t>((2 * 512 + 3) * 7),
           "the third lands at block 22");
    std::vector<std::byte> in(3 * 512);
    expect(card.read(20, in) == Status::Ok && in == out && sent(18) && sent(12),
           "three blocks read back with CMD18 and CMD12");
    expect(card.read(10, std::span{in}.first(512)) == Status::Ok &&
               std::equal(out.begin(), out.begin() + 512, in.begin()) && sent(17),
           "a single block reads back with CMD17");
    expect(sent(13), "each write is confirmed with CMD13");
    expect(mm_test_sdcard_deselected(), "chip select is high between operations");
}

void standard_capacity_cards_are_byte_addressed() {
    for (const int kind : {sdsc_version_2, sdsc_version_1}) {
        mm_test_sdcard_reset(kind, 2048);
        mm::sdcard::SpiCard card{wiring()};
        mm::fs::BlockGeometry geometry;
        expect(card.geometry(geometry) == Status::Ok && geometry.count == 2048,
               "an SDSC card's version 1 CSD gives its size");
        expect(sent(16), "the block length is set to 512");
        expect(kind == sdsc_version_2 ? sent(58) : !sent(58),
               "only a version 2 card is asked for its OCR");
        mm_test_sdcard_store(3 * 512, 0x5a);
        std::array<std::byte, 512> in{};
        expect(card.read(3, in) == Status::Ok && in[0] == std::byte{0x5a},
               "block 3 reads from byte 1536");
        expect(mm_test_sdcard_argument(mm_test_sdcard_commands() - 1) == 3 * 512,
               "the read's argument is a byte address");
    }
}

void arguments_are_checked_before_the_bus() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm::sdcard::SpiCard card{wiring()};
    std::array<std::byte, 100> partial{};
    std::array<std::byte, 512> block{};
    expect(card.read(0, partial) == Status::BadArgument &&
               card.write(0, std::span<const std::byte>{}) == Status::BadArgument,
           "partial and empty transfers are refused");
    expect(mm_test_sdcard_commands() == 0, "without touching the card");
    expect(card.read(2048, block) == Status::BadArgument &&
               card.write(2047, std::span{partial}.first(0)) == Status::BadArgument,
           "a block past the end is refused");
    std::vector<std::byte> two(1024);
    expect(card.read(2047, two) == Status::BadArgument, "so is a run past the end");
}

void a_missing_card_is_a_transport_error() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm_test_sdcard_present(false);
    mm::sdcard::SpiCard card{wiring()};
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::TransportError, "no card answers CMD0");
    expect(mm_test_sdcard_deselected(), "and chip select is left high");
    mm_test_sdcard_present(true);
    expect(card.geometry(geometry) == Status::Ok, "a card put in afterwards is found");
}

void a_card_that_never_leaves_idle_times_out() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm_test_sdcard_never_ready(true);
    mm::sdcard::SpiCard card{wiring()};
    mm::fs::BlockGeometry geometry;
    expect(card.geometry(geometry) == Status::Timeout, "ACMD41 polling gives up");
}

void a_corrupt_read_is_refused_and_the_card_found_again() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm::sdcard::SpiCard card{wiring()};
    std::array<std::byte, 512> in{};
    expect(card.read(0, in) == Status::Ok, "a clean read");
    mm_test_sdcard_corrupt_next_read();
    expect(card.read(0, in) == Status::TransportError, "a block whose CRC16 is wrong is refused");
    expect(card.read(0, in) == Status::Ok && mm_test_sdcard_count(0) == 2,
           "the next call initialises the card again and succeeds");
}

void a_card_pulled_mid_transfer_is_found_again() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm::sdcard::SpiCard card{wiring()};
    std::vector<std::byte> in(4 * 512);
    expect(card.read(0, std::span{in}.first(512)) == Status::Ok, "the card works");
    mm_test_sdcard_pull_after(600);
    const auto pulled = card.read(0, in);
    expect(pulled == Status::TransportError || pulled == Status::Timeout,
           "a card pulled during a multiple read fails the read");
    expect(card.read(0, in) == Status::TransportError, "and nothing answers while it is out");
    mm_test_sdcard_present(true);
    expect(card.read(0, in) == Status::Ok, "put back, it is initialised again and read");
}

void chip_select_frames_every_transaction() {
    mm_test_sdcard_reset(sdhc, 2048);
    mm::sdcard::SpiCard card{wiring()};
    std::array<std::byte, 512> block{};
    expect(card.write(5, block) == Status::Ok && card.read(5, block) == Status::Ok,
           "a write and a read");
    expect(mm_test_sdcard_deselected(), "end with chip select high");
    expect(mm_test_sdcard_clocks_deselected() >= 10,
           "and the wake-up clocks were sent with it high");
}

void an_unbound_socket_is_unsupported() {
    // Nothing in this binary binds a socket provider.
    auto& card = mm::sdcard::socket::card();
    mm::fs::BlockGeometry geometry{9, 9};
    std::array<std::byte, 512> block{};
    expect(card.geometry(geometry) == Status::Unsupported && geometry.count == 9 &&
               card.read(0, block) == Status::Unsupported &&
               card.write(0, block) == Status::Unsupported,
           "the fallback socket's card answers Unsupported and changes nothing");
}

const mm::test::case_ cases[] = {
    {"the CRCs match the specification", &the_crcs_match_the_specification},
    {"an SDHC card initialises", &an_sdhc_card_initializes},
    {"blocks round trip on SDHC", &blocks_round_trip_on_sdhc},
    {"standard-capacity cards are byte addressed", &standard_capacity_cards_are_byte_addressed},
    {"arguments are checked before the bus", &arguments_are_checked_before_the_bus},
    {"a missing card is a transport error", &a_missing_card_is_a_transport_error},
    {"a card that never leaves idle times out", &a_card_that_never_leaves_idle_times_out},
    {"a corrupt read is refused and the card found again",
     &a_corrupt_read_is_refused_and_the_card_found_again},
    {"a card pulled mid-transfer is found again", &a_card_pulled_mid_transfer_is_found_again},
    {"chip select frames every transaction", &chip_select_frames_every_transaction},
    {"an unbound socket is Unsupported", &an_unbound_socket_is_unsupported},
};

const mm::test::registrar reg{"mm.sdcard", cases};

}  // namespace
