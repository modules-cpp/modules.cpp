// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Linux's own FatFs, platform.linux.fs.fat over the linux-fatfs library: the
// mm.fs contract on a RAM block device and on the storage-linux emulator's SD
// card through mm.sdcard, an unformatted device left unwritten, and images
// that host tools make and check, when dosfstools is installed.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

import mm.fs;
import mm.fs.conformance;
import mm.fs.fat;
import mm.mcu;
import mm.sdcard;
import mm.test;
import platform.linux.fs.fat;
import platform.linux.storage.store;
import platform.linux.storage.sdcard;
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

constexpr std::string_view prefix = "/fat";

// 512-byte blocks in memory, counting writes.
class RamDisk final : public mm::fs::BlockDevice {
public:
    explicit RamDisk(std::size_t blocks) : bytes_(blocks * 512, std::byte{0xa5}) {}

    [[nodiscard]] Status geometry(mm::fs::BlockGeometry& geometry) override {
        geometry = {bytes_.size() / 512, 512};
        return Status::Ok;
    }
    [[nodiscard]] Status read(std::uint64_t block, std::span<std::byte> data) override {
        if (!fits(block, data.size())) return Status::BadArgument;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(block * 512), data.size(),
                    data.begin());
        return Status::Ok;
    }
    [[nodiscard]] Status write(std::uint64_t block, std::span<const std::byte> data) override {
        if (!fits(block, data.size())) return Status::BadArgument;
        std::copy(data.begin(), data.end(),
                  bytes_.begin() + static_cast<std::ptrdiff_t>(block * 512));
        ++writes_;
        return Status::Ok;
    }
    [[nodiscard]] Status sync() override { return Status::Ok; }
    [[nodiscard]] unsigned long writes() const { return writes_; }

private:
    [[nodiscard]] bool fits(std::uint64_t block, std::size_t size) const {
        return size != 0 && size % 512 == 0 && block <= bytes_.size() / 512 &&
               size / 512 <= bytes_.size() / 512 - block;
    }

    std::vector<std::byte> bytes_;
    unsigned long writes_ = 0;
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

mm::sdcard::SpiCard bus_card() {
    return mm::sdcard::SpiCard{{.spi = {.instance = BusWiring::instance,
                                        .clock_gpio = BusWiring::clock_gpio,
                                        .transmit_gpio = BusWiring::transmit_gpio,
                                        .receive_gpio = BusWiring::receive_gpio,
                                        .baud = 0,
                                        .mode = mm::mcu::SpiMode::Mode0,
                                        .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
                                .chip_select_gpio = BusWiring::sdcard_chip_select_gpio}};
}

// A failed expectation leaves its case early, possibly still mounted.
void clear() { static_cast<void>(mm::fs::fat::unmount(prefix)); }

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

// A host tool by name: on PATH, or in the sbin directories dosfstools
// installs into, which an ordinary user's PATH often lacks.
std::filesystem::path host_tool(std::string_view name) {
    std::vector<std::filesystem::path> directories{"/usr/sbin", "/sbin"};
    if (const char* path = std::getenv("PATH")) {
        std::string_view rest{path};
        while (!rest.empty()) {
            const auto colon = rest.find(':');
            const auto entry = rest.substr(0, colon);
            if (!entry.empty()) directories.emplace_back(std::string{entry});
            if (colon == std::string_view::npos) break;
            rest.remove_prefix(colon + 1);
        }
    }
    for (const auto& directory : directories) {
        std::error_code error;
        const auto candidate = directory / std::string{name};
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return {};
}

int run_quietly(const std::filesystem::path& tool, const std::string& arguments) {
    const std::string command = "'" + tool.string() + "' " + arguments + " >/dev/null 2>&1";
    return std::system(command.c_str());
}

void fat_on_a_ram_disk_keeps_the_contract() {
    clear();
    RamDisk disk{8192};
    expect(mm::fs::fat::format(disk) == Status::Ok, "a 4 MiB disk formats");
    expect(mm::fs::fat::mount(prefix, disk) == Status::Ok, "and mounts");
    conform("FAT on a RAM disk passes every conformance check");
    expect(mm::fs::fat::unmount(prefix) == Status::Ok, "and unmounts");
}

void an_unformatted_device_is_left_unwritten() {
    clear();
    RamDisk disk{2048};
    expect(mm::fs::fat::mount(prefix, disk) == Status::Corrupt,
           "a device holding no FAT volume is Corrupt");
    expect(disk.writes() == 0, "and nothing was written to it");
}

void fat_on_the_emulated_card_through_sdcard() {
    clear();
    MemoryStore store{8u << 20, std::byte{0}};
    SdCard card{store, SdKind::Sdhc};
    Bus bus{&card, nullptr};
    const Selected selected{bus};
    auto driver = bus_card();

    expect(mm::fs::fat::format(driver) == Status::Ok, "the emulated SD card formats");
    expect(mm::fs::fat::mount(prefix, driver) == Status::Ok, "and mounts");
    conform("FAT through mm.sdcard passes every conformance check");
    expect(write_text("/fat/kept.txt", "on the card"), "a file writes");
    expect(mm::fs::fat::unmount(prefix) == Status::Ok, "and unmounts");
    expect(mm::fs::fat::mount(prefix, driver) == Status::Ok &&
               reads_text("/fat/kept.txt", "on the card"),
           "a remount reads it back");
    expect(mm::fs::fat::unmount(prefix) == Status::Ok, "and unmounts again");
    expect(bus.wiring_errors() == 0, "only the card was selected");
}

void host_tools_make_and_check_the_images() {
    clear();
    const auto mkfs = host_tool("mkfs.fat");
    const auto fsck = host_tool("fsck.fat");
    if (mkfs.empty() || fsck.empty()) return;    // dosfstools is not installed: skipped

    const mm::test::scoped_tree tree{"linux_fat_host_tools"};
    std::error_code error;
    std::filesystem::create_directories(tree.root(), error);
    const auto image = tree.root() / "sdcard.img";
    expect(run_quietly(mkfs, "-F 32 -C '" + image.string() + "' 33792") == 0,
           "mkfs.fat makes a 33 MiB FAT32 image");
    {
        FileStore store{image, 0, std::byte{0}};
        SdCard card{store, SdKind::Sdhc};
        Bus bus{&card, nullptr};
        const Selected selected{bus};
        auto driver = bus_card();
        expect(store.size() == 33792u * 1024 && mm::fs::fat::mount(prefix, driver) == Status::Ok,
               "the emulated card mounts the host's image");
        expect(mm::fs::make_directory("/fat/logs") == Status::Ok &&
                   write_text("/fat/logs/a long file name.txt", "from the emulator"),
               "a directory and a long-named file write");
        expect(mm::fs::fat::unmount(prefix) == Status::Ok, "and unmounts");
    }
    expect(run_quietly(fsck, "-n '" + image.string() + "'") == 0,
           "fsck.fat finds the image clean after the emulator wrote it");
    FileStore store{image, 0, std::byte{0}};
    SdCard card{store, SdKind::Sdhc};
    Bus bus{&card, nullptr};
    const Selected selected{bus};
    auto driver = bus_card();
    expect(mm::fs::fat::mount(prefix, driver) == Status::Ok &&
               reads_text("/fat/logs/a long file name.txt", "from the emulator"),
           "and a new card over the image reads the file back");
    expect(mm::fs::fat::unmount(prefix) == Status::Ok, "and unmounts");
}

const mm::test::case_ cases[]{
    {"platform.linux.fs.fat on a RAM disk", fat_on_a_ram_disk_keeps_the_contract},
    {"platform.linux.fs.fat unformatted device", an_unformatted_device_is_left_unwritten},
    {"platform.linux.fs.fat on the emulated card", fat_on_the_emulated_card_through_sdcard},
    {"platform.linux.fs.fat host tools", host_tools_make_and_check_the_images},
};
const mm::test::registrar registrar{"platform.linux.fs.fat", cases};

}  // namespace
