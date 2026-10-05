// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Linux's own littlefs, platform.linux.fs.littlefs over the linux-littlefs
// library: the mm.fs contract on a RAM flash device and on the storage-linux
// emulator's W25Q through mm.spiflash, a volume that outlives its process in
// an image file, and a blank device left alone.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

import mm.fs;
import mm.fs.conformance;
import mm.fs.littlefs;
import mm.mcu;
import mm.spiflash;
import mm.test;
import platform.linux.fs.littlefs;
import platform.linux.storage.store;
import platform.linux.storage.spiflash;
import platform.linux.storage.bus;

namespace {

using mm::fs::Status;
using mm::test::expect;
using platform::linux::storage::Bus;
using platform::linux::storage::BusWiring;
using platform::linux::storage::FileStore;
using platform::linux::storage::MemoryStore;
using platform::linux::storage::SpiFlash;

constexpr std::string_view prefix = "/lfs";

// NOR flash in memory: erase sets 0xFF, program may only clear bits.
class RamFlash final : public mm::fs::FlashDevice {
public:
    explicit RamFlash(std::size_t size) : bytes_(size, std::byte{0xff}) {}

    [[nodiscard]] Status geometry(mm::fs::FlashGeometry& geometry) override {
        geometry = {1, 256, 4096, static_cast<std::uint32_t>(bytes_.size() / 4096)};
        return Status::Ok;
    }
    [[nodiscard]] Status read(std::uint64_t offset, std::span<std::byte> data) override {
        if (offset > bytes_.size() || data.size() > bytes_.size() - offset)
            return Status::BadArgument;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), data.size(),
                    data.begin());
        return Status::Ok;
    }
    [[nodiscard]] Status program(std::uint64_t offset, std::span<const std::byte> data) override {
        if (offset % 256 != 0 || data.size() % 256 != 0 || offset > bytes_.size() ||
            data.size() > bytes_.size() - offset)
            return Status::BadArgument;
        for (std::size_t i = 0; i < data.size(); ++i) {
            auto& cell = bytes_[static_cast<std::size_t>(offset) + i];
            if ((data[i] & ~cell) != std::byte{0}) ++unerased_;
            cell &= data[i];
        }
        return Status::Ok;
    }
    [[nodiscard]] Status erase(std::uint64_t offset, std::uint64_t size) override {
        if (offset % 4096 != 0 || size % 4096 != 0 || offset > bytes_.size() ||
            size > bytes_.size() - offset)
            return Status::BadArgument;
        std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), size, std::byte{0xff});
        return Status::Ok;
    }
    [[nodiscard]] Status sync() override { return Status::Ok; }

    [[nodiscard]] unsigned long unerased() const { return unerased_; }

private:
    std::vector<std::byte> bytes_;
    unsigned long unerased_ = 0;
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

mm::spiflash::Chip bus_flash() {
    return mm::spiflash::Chip{{.spi = {.instance = BusWiring::instance,
                                       .clock_gpio = BusWiring::clock_gpio,
                                       .transmit_gpio = BusWiring::transmit_gpio,
                                       .receive_gpio = BusWiring::receive_gpio,
                                       .baud = 0,
                                       .mode = mm::mcu::SpiMode::Mode0,
                                       .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
                               .chip_select_gpio = BusWiring::spiflash_chip_select_gpio}};
}

// A failed expectation leaves its case early, possibly still mounted.
void clear() { static_cast<void>(mm::fs::littlefs::unmount(prefix)); }

void conform(std::string_view what) {
    mm::fs::conformance::Report report;
    expect(mm::fs::conformance::run(prefix, report) == Status::Ok, "the checks can run");
    expect(report.failed == 0 && report.passed > 0, what);
    if (report.failed != 0) expect(false, report.first_failure);
}

bool write_text(std::string_view path, std::string_view text) {
    mm::fs::File file;
    if (mm::fs::open(path, mm::fs::Access::Write, mm::fs::Disposition::CreateOrTruncate, file) !=
        Status::Ok)
        return false;
    std::size_t count = 0;
    return file.write(std::as_bytes(std::span{text.data(), text.size()}), count) == Status::Ok &&
           count == text.size() && file.close() == Status::Ok;
}

bool reads_text(std::string_view path, std::string_view text) {
    mm::fs::File file;
    if (mm::fs::open(path, mm::fs::Access::Read, mm::fs::Disposition::OpenExisting, file) !=
        Status::Ok)
        return false;
    std::array<char, 64> buffer{};
    std::size_t count = 0;
    if (file.read(std::as_writable_bytes(std::span{buffer}), count) != Status::Ok) return false;
    return std::string_view{buffer.data(), count} == text;
}

void littlefs_on_ram_flash_keeps_the_contract() {
    clear();
    RamFlash flash{256 * 1024};
    expect(mm::fs::littlefs::format(flash) == Status::Ok, "a blank device formats");
    expect(mm::fs::littlefs::mount(prefix, flash) == Status::Ok, "and mounts");
    conform("littlefs on RAM flash passes every conformance check");
    expect(flash.unerased() == 0, "littlefs never programs unerased bits");
    expect(mm::fs::littlefs::unmount(prefix) == Status::Ok, "and unmounts");
}

void a_blank_device_is_left_alone() {
    clear();
    RamFlash flash{64 * 1024};
    expect(mm::fs::littlefs::mount(prefix, flash) == Status::Corrupt,
           "a device holding no littlefs is Corrupt, not formatted");
    mm::fs::Stat stat;
    expect(mm::fs::stat(prefix, stat) == Status::NotFound, "nothing is left mounted");
}

void littlefs_on_the_emulated_chip_through_spiflash() {
    clear();
    MemoryStore store{1u << 20, std::byte{0xff}};
    SpiFlash chip{store};
    chip.busy_reads(2);
    Bus bus{nullptr, &chip};
    const Selected selected{bus};
    auto driver = bus_flash();

    expect(mm::fs::littlefs::format(driver) == Status::Ok, "the emulated W25Q formats");
    expect(mm::fs::littlefs::mount(prefix, driver) == Status::Ok, "and mounts");
    conform("littlefs through mm.spiflash passes every conformance check");
    expect(write_text("/lfs/kept.txt", "on the chip"), "a file writes");
    expect(mm::fs::littlefs::unmount(prefix) == Status::Ok, "and unmounts");
    expect(mm::fs::littlefs::mount(prefix, driver) == Status::Ok &&
               reads_text("/lfs/kept.txt", "on the chip"),
           "a remount reads it back");
    expect(chip.unerased_bits() == 0 && chip.unenabled_writes() == 0 &&
               chip.busy_violations() == 0,
           "littlefs and the driver never program unerased bits, skip write enable, or "
           "ignore BUSY");
    expect(chip.programs() > 0 && chip.erases() > 0, "the chip was programmed and erased");
    expect(mm::fs::littlefs::unmount(prefix) == Status::Ok, "and unmounts again");
}

void a_volume_outlives_its_image_file() {
    clear();
    const mm::test::scoped_tree tree{"linux_littlefs_image"};
    std::error_code error;
    std::filesystem::create_directories(tree.root(), error);
    const auto path = tree.root() / "spiflash.img";
    {
        FileStore store{path, 1u << 16, std::byte{0xff}};
        SpiFlash chip{store};
        Bus bus{nullptr, &chip};
        const Selected selected{bus};
        auto driver = bus_flash();
        expect(mm::fs::littlefs::format(driver) == Status::Ok &&
                   mm::fs::littlefs::mount(prefix, driver) == Status::Ok &&
                   write_text("/lfs/note.txt", "in the image") &&
                   mm::fs::littlefs::unmount(prefix) == Status::Ok,
               "a 64 KiB image gets a volume and a file");
    }
    FileStore store{path, 0, std::byte{0xff}};
    SpiFlash chip{store};
    Bus bus{nullptr, &chip};
    const Selected selected{bus};
    auto driver = bus_flash();
    expect(store.size() == (1u << 16) && mm::fs::littlefs::mount(prefix, driver) == Status::Ok &&
               reads_text("/lfs/note.txt", "in the image"),
           "a new chip over the same file mounts it and reads the file");
    expect(mm::fs::littlefs::unmount(prefix) == Status::Ok, "and unmounts");
}

const mm::test::case_ cases[]{
    {"platform.linux.fs.littlefs on RAM flash", littlefs_on_ram_flash_keeps_the_contract},
    {"platform.linux.fs.littlefs blank device", a_blank_device_is_left_alone},
    {"platform.linux.fs.littlefs on the emulated chip",
     littlefs_on_the_emulated_chip_through_spiflash},
    {"platform.linux.fs.littlefs image file", a_volume_outlives_its_image_file},
};
const mm::test::registrar registrar{"platform.linux.fs.littlefs", cases};

}  // namespace
