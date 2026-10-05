// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The storage-linux emulation: the virtual SD card and SPI flash on their
// own, and mm.sdcard and mm.spiflash driving them through the bus exactly as
// they drive real chips. Each case makes its own bus over in-memory stores and
// selects it for its duration, so nothing here touches a file but the
// FileStore case's own.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.spiflash;
import mm.test;
import platform.linux.defaults;
import platform.linux.map;
import platform.linux.storage.store;
import platform.linux.storage.sdcard;
import platform.linux.storage.spiflash;
import platform.linux.storage.bus;

namespace {

using mm::fs::Status;
using mm::test::expect;
using platform::linux::storage::Bus;
using platform::linux::storage::BusWiring;
using platform::linux::storage::FileStore;
using platform::linux::storage::MemoryStore;
using platform::linux::storage::SdCard;
using platform::linux::storage::SdKind;
using platform::linux::storage::SpiFlash;

// Puts the real platform back when the case leaves scope, even on failure.
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

mm::mcu::SpiConfiguration bus_spi() {
    return {.instance = BusWiring::instance,
            .clock_gpio = BusWiring::clock_gpio,
            .transmit_gpio = BusWiring::transmit_gpio,
            .receive_gpio = BusWiring::receive_gpio,
            .baud = 0,
            .mode = mm::mcu::SpiMode::Mode0,
            .bit_order = mm::mcu::BitOrder::MostSignificantFirst};
}

mm::sdcard::SpiCard bus_card() {
    return mm::sdcard::SpiCard{
        {.spi = bus_spi(), .chip_select_gpio = BusWiring::sdcard_chip_select_gpio}};
}

mm::spiflash::Chip bus_flash() {
    return mm::spiflash::Chip{
        {.spi = bus_spi(), .chip_select_gpio = BusWiring::spiflash_chip_select_gpio}};
}

std::vector<std::byte> pattern(std::size_t size, unsigned int seed) {
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::byte>((i * 31 + seed * 17 + 3) & 0xffu);
    return bytes;
}

bool contains(const std::vector<unsigned int>& commands, unsigned int index) {
    return std::find(commands.begin(), commands.end(), index) != commands.end();
}

bool contains(const std::vector<std::uint8_t>& commands, std::uint8_t opcode) {
    return std::find(commands.begin(), commands.end(), opcode) != commands.end();
}

long count_of(const std::vector<std::uint8_t>& commands, std::uint8_t opcode) {
    return std::count(commands.begin(), commands.end(), opcode);
}

// A command frame clocked straight into the card, and up to eight bytes
// after it for the R1; answers 0xFF when nothing came.
std::uint8_t raw_command(SdCard& card, unsigned int index, std::uint32_t argument,
                         bool good_crc = true) {
    std::uint8_t frame[6]{static_cast<std::uint8_t>(0x40u | index),
                          static_cast<std::uint8_t>(argument >> 24),
                          static_cast<std::uint8_t>(argument >> 16),
                          static_cast<std::uint8_t>(argument >> 8),
                          static_cast<std::uint8_t>(argument), 0};
    frame[5] = static_cast<std::uint8_t>((platform::linux::storage::sd_crc7(frame, 5) << 1) | 1u);
    if (!good_crc) frame[5] ^= 0x10u;
    for (const auto value : frame) static_cast<void>(card.exchange(value));
    for (int i = 0; i < 8; ++i) {
        const auto got = card.exchange(0xff);
        if ((got & 0x80u) == 0) return got;
    }
    return 0xff;
}

void the_sd_crcs_match_the_drivers() {
    const std::uint8_t command0[5]{0x40, 0, 0, 0, 0};
    expect(platform::linux::storage::sd_crc7(command0, 5) == 0x4a, "CMD0's CRC7 is 0x4A");
    const std::uint8_t command8[5]{0x48, 0, 0, 0x01, 0xaa};
    expect(platform::linux::storage::sd_crc7(command8, 5) == 0x43, "CMD8's CRC7 is 0x43");
    const std::vector<std::uint8_t> ones(512, 0xff);
    expect(platform::linux::storage::sd_crc16(ones.data(), ones.size()) == 0x7fa1,
           "512 bytes of 0xFF have CRC16 0x7FA1");
    const auto data = pattern(512, 1);
    expect(platform::linux::storage::sd_crc16(reinterpret_cast<const std::uint8_t*>(data.data()),
                                              data.size()) == mm::sdcard::crc16(data),
           "the card and the driver agree on CRC16");
}

void the_card_model_follows_spi_mode() {
    MemoryStore store{1u << 20, std::byte{0}};
    SdCard card{store, SdKind::Sdhc};
    expect(card.exchange(0x40) == 0xff, "a deselected card answers nothing");
    card.chip_select(false);
    expect(raw_command(card, 0, 0, false) == 0xff, "CMD0 with a bad CRC is ignored");
    expect(raw_command(card, 0, 0) == 0x01, "CMD0 enters SPI mode, idle");
    expect(raw_command(card, 17, 0) == 0x05, "a read before initialisation is illegal");
    expect(raw_command(card, 8, 0x1aa, false) == 0x09, "CMD8 is always CRC-checked");
    expect(raw_command(card, 55, 0) == 0x01 && raw_command(card, 41, 0) == 0x01 &&
               raw_command(card, 55, 0) == 0x01 && raw_command(card, 41, 0) == 0x01 &&
               raw_command(card, 55, 0) == 0x01 && raw_command(card, 41, 0) == 0x01 &&
               raw_command(card, 55, 0) == 0x01 && raw_command(card, 41, 0) == 0x01,
           "an SDHC card asked without HCS stays idle");
    std::uint8_t r1 = 0xff;
    for (int i = 0; i < 5 && r1 != 0x00; ++i) {
        static_cast<void>(raw_command(card, 55, 0));
        r1 = raw_command(card, 41, 0x4000'0000u);
    }
    expect(r1 == 0x00, "ACMD41 with HCS leaves idle after a few polls");
    expect(raw_command(card, 17, 2048) == 0x20, "a block past the end is an address error");
    expect(raw_command(card, 16, 1024) == 0x40, "a block length other than 512 is refused");
    expect(raw_command(card, 59, 1) == 0x00 && card.crc_enabled(), "CMD59 turns CRC checks on");
    expect(raw_command(card, 13, 0, false) == 0x08, "with CRC on, a bad CRC7 is a CRC error");
    card.remove();
    expect(raw_command(card, 13, 0) == 0xff && !card.present(), "a removed card answers nothing");
}

void sdhc_reads_and_writes_through_the_bus() {
    MemoryStore store{1u << 20, std::byte{0}};
    SdCard card{store, SdKind::Sdhc};
    Bus bus{&card, nullptr};
    const Selected selected{bus};
    auto driver = bus_card();

    mm::fs::BlockGeometry geometry;
    expect(driver.geometry(geometry) == Status::Ok && geometry.count == 2048 &&
               geometry.size == 512,
           "the card's CSD describes the store: 2048 blocks of 512");
    expect(contains(card.commands(), 8) && contains(card.commands(), 58) &&
               contains(card.commands(), 59) && contains(card.commands(), 9) &&
               !contains(card.commands(), 16),
           "an SDHC card is identified without CMD16");

    const auto one = pattern(512, 2);
    expect(driver.write(5, one) == Status::Ok, "a single block writes");
    expect(std::equal(one.begin(), one.end(), store.bytes().begin() + 5 * 512),
           "the block lands at block 5's bytes: block addressing");
    const auto many = pattern(4 * 512, 3);
    expect(driver.write(100, many) == Status::Ok, "four blocks write with CMD25");
    std::vector<std::byte> back(4 * 512);
    expect(driver.read(100, back) == Status::Ok && back == many,
           "four blocks read back with CMD18 and CMD12");
    std::vector<std::byte> single(512);
    expect(driver.read(5, single) == Status::Ok && single == one, "a single block reads back");
    expect(contains(card.commands(), 25) && contains(card.commands(), 18) &&
               contains(card.commands(), 12) && contains(card.commands(), 13),
           "multiple-block commands and CMD13 were used");
    expect(driver.read(2047, single) == Status::Ok, "the last block reads");
    expect(driver.read(2048, single) == Status::BadArgument, "past the end is refused");
    expect(bus.wiring_errors() == 0, "only one chip was ever selected");
}

void sdsc_cards_are_byte_addressed() {
    for (const auto kind : {SdKind::SdscVersion2, SdKind::SdscVersion1}) {
        MemoryStore store{1u << 20, std::byte{0}};
        SdCard card{store, kind};
        Bus bus{&card, nullptr};
        const Selected selected{bus};
        auto driver = bus_card();
        mm::fs::BlockGeometry geometry;
        expect(driver.geometry(geometry) == Status::Ok && geometry.count == 2048,
               "a version 1 CSD describes the store too");
        expect(contains(card.commands(), 16), "a standard-capacity card is given CMD16");
        const auto data = pattern(1024, 4);
        expect(driver.write(3, data) == Status::Ok, "two blocks write");
        expect(std::equal(data.begin(), data.end(), store.bytes().begin() + 3 * 512),
               "byte address 1536 is block 3");
        std::vector<std::byte> back(1024);
        expect(driver.read(3, back) == Status::Ok && back == data, "and read back");
    }
}

void card_faults_reach_the_driver() {
    MemoryStore store{1u << 20, std::byte{0}};
    SdCard card{store, SdKind::Sdhc};
    Bus bus{&card, nullptr};
    const Selected selected{bus};
    auto driver = bus_card();
    mm::fs::BlockGeometry geometry;

    card.remove();
    expect(driver.geometry(geometry) == Status::TransportError, "an empty socket is TransportError");
    card.insert();
    expect(driver.geometry(geometry) == Status::Ok, "an inserted card is found again");

    const auto data = pattern(512, 5);
    card.write_protect(true);
    expect(driver.write(7, data) == Status::TransportError, "a protected card refuses a write");
    expect(std::all_of(store.bytes().begin() + 7 * 512, store.bytes().begin() + 8 * 512,
                       [](std::byte b) { return b == std::byte{0}; }),
           "and nothing was written");
    card.write_protect(false);
    expect(driver.write(7, data) == Status::Ok, "the driver re-identifies and writes after");

    card.corrupt_next_read();
    std::vector<std::byte> back(512);
    expect(driver.read(7, back) == Status::TransportError, "a bad data CRC16 is TransportError");
    expect(driver.read(7, back) == Status::Ok && back == std::vector<std::byte>(data),
           "the next read is good");

    card.fail_writes_after(1);
    const auto two = pattern(1024, 6);
    expect(driver.write(20, two) == Status::TransportError,
           "a write failing on its second block is TransportError");
    expect(std::equal(two.begin(), two.begin() + 512, store.bytes().begin() + 20 * 512),
           "the first block was written");
    card.fail_writes_after(-1);
    expect(driver.write(20, two) == Status::Ok, "and the card works again after");
}

// A command and its bytes clocked straight into the chip, chip select
// rising after.
std::vector<std::uint8_t> raw(SpiFlash& chip, std::initializer_list<std::uint8_t> out,
                              std::size_t extra = 0) {
    std::vector<std::uint8_t> in;
    chip.chip_select(false);
    for (const auto value : out) in.push_back(chip.exchange(value));
    for (std::size_t i = 0; i < extra; ++i) in.push_back(chip.exchange(0xff));
    chip.chip_select(true);
    return in;
}

void the_flash_model_behaves_as_nor() {
    MemoryStore store{1u << 16, std::byte{0xff}};
    SpiFlash chip{store};
    const auto id = raw(chip, {0x9f}, 3);
    expect(id[1] == 0xef && id[2] == 0x40 && id[3] == 0x10, "64 KiB answers JEDEC EF 40 10");

    static_cast<void>(raw(chip, {0x02, 0x00, 0x01, 0x00, 0x12}));
    expect(store.bytes()[0x100] == std::byte{0xff} && chip.unenabled_writes() == 1,
           "a program without write enable is ignored and counted");

    static_cast<void>(raw(chip, {0x06}));
    expect(chip.write_enabled(), "write enable sets WEL");
    static_cast<void>(raw(chip, {0x02, 0x00, 0x01, 0xfe, 0xa1, 0xa2, 0xa3, 0xa4}));
    expect(store.bytes()[0x1fe] == std::byte{0xa1} && store.bytes()[0x1ff] == std::byte{0xa2} &&
               store.bytes()[0x100] == std::byte{0xa3} && store.bytes()[0x101] == std::byte{0xa4},
           "a program past the page's end wraps to its start");
    expect(!chip.write_enabled() && chip.busy(), "a program clears WEL and sets BUSY");
    const auto early = raw(chip, {0x9f}, 3);
    expect(early[1] == 0xff && chip.busy_violations() == 1,
           "a command while busy is ignored and counted");
    const auto status = raw(chip, {0x05}, 2);
    expect((status[1] & 0x01u) != 0 && (status[2] & 0x01u) == 0,
           "BUSY shows in one status read, then clears");

    static_cast<void>(raw(chip, {0x06}));
    static_cast<void>(raw(chip, {0x02, 0x00, 0x01, 0x00, 0x0f}));
    expect(store.bytes()[0x100] == std::byte{0xa3 & 0x0f} && chip.unerased_bits() == 2,
           "program ANDs: bits set from 0 to 1 are counted and left");

    static_cast<void>(raw(chip, {0x05}, 1));
    static_cast<void>(raw(chip, {0x06}));
    static_cast<void>(raw(chip, {0x20, 0x00, 0x01, 0x23}));
    static_cast<void>(raw(chip, {0x05}, 1));
    expect(std::all_of(store.bytes().begin(), store.bytes().begin() + 4096,
                       [](std::byte b) { return b == std::byte{0xff}; }),
           "a sector erase sets the whole 4 KiB sector to 0xFF");

    static_cast<void>(raw(chip, {0xb9}));
    expect(raw(chip, {0x9f}, 1)[1] == 0xff && chip.powered_down(),
           "in power-down the chip answers nothing");
    static_cast<void>(raw(chip, {0xab}));
    expect(raw(chip, {0x9f}, 1)[1] == 0xef, "release power-down wakes it");

    static_cast<void>(raw(chip, {0x06}));
    static_cast<void>(raw(chip, {0x99}));
    expect(chip.write_enabled(), "reset without reset enable does nothing");
    static_cast<void>(raw(chip, {0x66}));
    static_cast<void>(raw(chip, {0x99}));
    expect(!chip.write_enabled(), "reset enable then reset clears the latches");

    MemoryStore big{1u << 24, std::byte{0xff}};
    SpiFlash w25q128{big};
    expect(raw(w25q128, {0x9f}, 3)[3] == 0x18, "16 MiB answers capacity 0x18, a W25Q128");
}

void spiflash_drives_the_chip_through_the_bus() {
    MemoryStore store{1u << 20, std::byte{0xff}};
    SpiFlash chip{store};
    chip.busy_reads(3);
    Bus bus{nullptr, &chip};
    const Selected selected{bus};
    auto driver = bus_flash();

    mm::fs::FlashGeometry geometry;
    expect(driver.geometry(geometry) == Status::Ok && geometry.read_size == 1 &&
               geometry.program_size == 256 && geometry.erase_size == 4096 &&
               geometry.erase_count == 256,
           "geometry from the JEDEC capacity: 1 MiB of 4 KiB sectors");
    expect(driver.jedec_id() == 0xef4014, "the JEDEC ID is kept");

    std::fill(store.bytes().begin(), store.bytes().end(), std::byte{0x00});
    expect(driver.erase(0, 72 * 1024) == Status::Ok, "72 KiB erases");
    expect(count_of(chip.commands(), 0xd8) == 1 && count_of(chip.commands(), 0x20) == 2,
           "one 64 KiB block erase, then two 4 KiB sector erases");
    expect(std::all_of(store.bytes().begin(), store.bytes().begin() + 72 * 1024,
                       [](std::byte b) { return b == std::byte{0xff}; }) &&
               store.bytes()[72 * 1024] == std::byte{0x00},
           "exactly the range is erased");

    const auto data = pattern(3 * 256, 7);
    expect(driver.program(4096 + 256, data) == Status::Ok, "three pages program");
    std::vector<std::byte> back(3 * 256);
    expect(driver.read(4096 + 256, back) == Status::Ok && back == data, "and read back");
    std::vector<std::byte> odd(5);
    expect(driver.read(4096 + 257, odd) == Status::Ok &&
               std::equal(odd.begin(), odd.end(), data.begin() + 1),
           "a read starts at any byte");
    expect(chip.unerased_bits() == 0 && chip.unenabled_writes() == 0 &&
               chip.busy_violations() == 0,
           "the driver never programs unerased bytes, never skips write enable, and waits");

    expect(driver.program(100, data) == Status::BadArgument, "an unaligned program is refused");
    expect(driver.erase(100, 4096) == Status::BadArgument, "an unaligned erase is refused");
    expect(driver.read((1u << 20) - 1, odd) == Status::BadArgument, "past the end is refused");
    expect(driver.sync() == Status::Ok, "sync has nothing to do");
}

void flash_absence_and_size_are_reported() {
    {
        Bus bus{nullptr, nullptr};
        const Selected selected{bus};
        auto driver = bus_flash();
        mm::fs::FlashGeometry geometry;
        expect(driver.geometry(geometry) == Status::TransportError,
               "no chip on the select line is TransportError, without waiting");
    }
    {
        MemoryStore store{1u << 15, std::byte{0xff}};
        SpiFlash chip{store};
        Bus bus{nullptr, &chip};
        const Selected selected{bus};
        auto driver = bus_flash();
        mm::fs::FlashGeometry geometry;
        expect(driver.geometry(geometry) == Status::Unsupported,
               "a capacity below 64 KiB is Unsupported");
    }
}

void the_bus_shares_spi_between_the_chips() {
    MemoryStore card_store{1u << 20, std::byte{0}};
    MemoryStore flash_store{1u << 16, std::byte{0xff}};
    SdCard card{card_store, SdKind::Sdhc};
    SpiFlash chip{flash_store};
    Bus bus{&card, &chip};
    const Selected selected{bus};

    expect(mm::mcu::board().name == "storage-linux" && mm::mcu::board().spi.has_value() &&
               mm::mcu::board().spi->chip_select_gpio == BusWiring::sdcard_chip_select_gpio,
           "the board names its SPI wiring and the card's select");
    auto wrong = bus_spi();
    wrong.baud = 1'000'000;
    wrong.clock_gpio = 2;
    expect(mm::mcu::spi_configure(wrong) == mm::mcu::Status::BadArgument,
           "a configuration naming other pins is refused");

    auto sd = bus_card();
    auto flash = bus_flash();
    mm::fs::BlockGeometry block_geometry;
    mm::fs::FlashGeometry flash_geometry;
    const auto data = pattern(512, 8);
    expect(sd.geometry(block_geometry) == Status::Ok &&
               flash.geometry(flash_geometry) == Status::Ok && sd.write(1, data) == Status::Ok &&
               flash.erase(0, 4096) == Status::Ok &&
               flash.program(0, std::span{data}.first(256)) == Status::Ok,
           "both drivers work in turn on one bus");
    std::vector<std::byte> back(512);
    expect(sd.read(1, back) == Status::Ok && back == data, "the card kept its block");
    expect(flash.read(0, std::span{back}.first(256)) == Status::Ok &&
               std::equal(back.begin(), back.begin() + 256, data.begin()),
           "the flash kept its page");
    expect(bus.wiring_errors() == 0, "the drivers never selected both");

    std::array<std::byte, 1> out{std::byte{0x9f}};
    std::array<std::byte, 1> in{};
    auto right = bus_spi();
    right.baud = 1'000'000;
    static_cast<void>(mm::mcu::spi_configure(right));
    static_cast<void>(mm::mcu::gpio_write(BusWiring::sdcard_chip_select_gpio, false));
    static_cast<void>(mm::mcu::gpio_write(BusWiring::spiflash_chip_select_gpio, false));
    expect(mm::mcu::spi_transfer(0, out, in) == mm::mcu::Status::Ok && in[0] == std::byte{0xff} &&
               bus.wiring_errors() == 1,
           "a byte with both selected answers 0xFF and is counted");
    static_cast<void>(mm::mcu::gpio_write(BusWiring::sdcard_chip_select_gpio, true));
    static_cast<void>(mm::mcu::gpio_write(BusWiring::spiflash_chip_select_gpio, true));
}

void a_file_store_makes_and_keeps_its_image() {
    const mm::test::scoped_tree tree{"linux_storage_emulation"};
    const auto path = tree.root() / "card.img";
    std::error_code error;
    std::filesystem::create_directories(tree.root(), error);
    {
        FileStore store{path, 8192, std::byte{0xff}};
        expect(store.ok() && store.size() == 8192, "a missing file is made at its size");
        std::array<std::byte, 4> word{};
        expect(store.read(8188, word) && word[3] == std::byte{0xff}, "filled with the fill byte");
        const std::array<std::byte, 2> mark{std::byte{0x12}, std::byte{0x34}};
        expect(store.write(100, mark), "a write goes through");
        expect(!store.write(8191, mark) && !store.read(8192, word), "past the end fails");
    }
    expect(std::filesystem::file_size(path, error) == 8192, "the file is the store's size");
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    expect(bytes.size() == 8192 && bytes[100] == 0x12 && bytes[101] == 0x34,
           "the write is in the file");
    FileStore again{path, 65536, std::byte{0x00}};
    std::array<std::byte, 2> mark{};
    expect(again.ok() && again.size() == 8192 && again.read(100, mark) &&
               mark[0] == std::byte{0x12},
           "an existing file keeps its contents and its own size");
}

void the_map_names_the_images() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    expect(map.sdcard.path.empty() && map.sdcard.size == (64u << 20) &&
               map.sdcard.kind == platform::linux::SdCardKind::Sdhc &&
               map.spiflash.path.empty() && map.spiflash.size == (16u << 20),
           "defaults: 64 MiB SDHC and 16 MiB flash in the working directory");
    const mm::test::scoped_file good{"mm_linux_map_storage.mdy",
                                     "sdcard.path = \"/srv/card.img\"\n"
                                     "sdcard.size = 33554432\n"
                                     "sdcard.kind = sdsc\n"
                                     "spiflash.path = \"/srv/flash.img\"\n"
                                     "spiflash.size = 8388608\n"};
    expect(platform::linux::apply_override(map, good.path().string(), error) ==
                   platform::linux::MapStatus::Ok &&
               map.sdcard.path == "/srv/card.img" && map.sdcard.size == 33554432 &&
               map.sdcard.kind == platform::linux::SdCardKind::Sdsc &&
               map.spiflash.path == "/srv/flash.img" && map.spiflash.size == 8388608,
           "the five keys parse");

    struct Bad {
        const char* name;
        const char* text;
        const char* key;
    };
    const Bad bad[]{
        {"mm_linux_map_sd_rel.mdy", "sdcard.path = \"card.img\"\n", "sdcard.path"},
        {"mm_linux_map_sd_size.mdy", "sdcard.size = 1000\n", "sdcard.size"},
        {"mm_linux_map_sd_kind.mdy", "sdcard.kind = mmc\n", "sdcard.kind"},
        {"mm_linux_map_sf_rel.mdy", "spiflash.path = \"flash.img\"\n", "spiflash.path"},
        {"mm_linux_map_sf_pow.mdy", "spiflash.size = 3145728\n", "spiflash.size"},
        {"mm_linux_map_sf_big.mdy", "spiflash.size = 33554432\n", "spiflash.size"},
    };
    for (const auto& entry : bad) {
        platform::linux::Map fresh;
        const mm::test::scoped_file file{entry.name, entry.text};
        expect(platform::linux::apply_override(fresh, file.path().string(), error) ==
                       platform::linux::MapStatus::SyntaxError &&
                   error.key == entry.key,
               entry.key);
    }
}

const mm::test::case_ cases[]{
    {"platform.linux.storage sd crcs", the_sd_crcs_match_the_drivers},
    {"platform.linux.storage sd card model", the_card_model_follows_spi_mode},
    {"platform.linux.storage sdhc through the bus", sdhc_reads_and_writes_through_the_bus},
    {"platform.linux.storage sdsc byte addressing", sdsc_cards_are_byte_addressed},
    {"platform.linux.storage card faults", card_faults_reach_the_driver},
    {"platform.linux.storage flash model", the_flash_model_behaves_as_nor},
    {"platform.linux.storage spiflash through the bus", spiflash_drives_the_chip_through_the_bus},
    {"platform.linux.storage flash absence and size", flash_absence_and_size_are_reported},
    {"platform.linux.storage shared bus", the_bus_shares_spi_between_the_chips},
    {"platform.linux.storage file store", a_file_store_makes_and_keeps_its_image},
    {"platform.linux.storage map keys", the_map_names_the_images},
};
const mm::test::registrar registrar{"platform.linux.storage", cases};

}  // namespace
