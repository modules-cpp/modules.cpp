// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

import mm.fs;
import mm.fs.conformance;
import mm.fs.local;
import mm.fs.native;
import mm.mcu;
import mm.test;
import platform.linux.defaults;
import platform.linux.fs;
import platform.linux.map;
import platform.linux.mcu;

namespace {

using mm::fs::Access;
using mm::fs::Disposition;
using mm::fs::Status;
using mm::test::expect;

// A failed expectation leaves its case early, possibly still mounted; each
// case clears the suite's prefixes first so one failure is reported once.
void clear() {
    for (const std::string_view prefix : {"/host", "/ro", "/x", "/data"}) {
        static_cast<void>(mm::fs::native::unmount(prefix));
        static_cast<void>(mm::fs::local::unmount(prefix));
    }
}

[[nodiscard]] std::span<const std::byte> bytes(std::string_view text) {
    return std::as_bytes(std::span<const char>{text.data(), text.size()});
}

void write_host_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

[[nodiscard]] std::string read_host_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void map_directory_keys_parse_and_validate() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    const mm::test::scoped_file good{"mm_linux_fs_good.mdy",
                                     "directory.0.path = \"/srv/board\"\n"
                                     "directory.0.writable = yes\n"
                                     "directory.1.path = \"/srv/other\"\n"};
    expect(platform::linux::apply_override(map, good.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "valid directory keys parse");
    expect(map.directories.size() == 2 && map.directories[0].path == "/srv/board" &&
               map.directories[0].writable && !map.directories[1].writable,
           "two entries, the second read-only by default");

    platform::linux::Map relative;
    const mm::test::scoped_file rel{"mm_linux_fs_rel.mdy", "directory.0.path = \"srv\"\n"};
    expect(platform::linux::apply_override(relative, rel.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "directory.0.path" && error.reason == "relative path",
           "a relative directory is refused");

    platform::linux::Map pathless;
    const mm::test::scoped_file none{"mm_linux_fs_none.mdy", "directory.0.writable = yes\n"};
    expect(platform::linux::apply_override(pathless, none.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "directory.0.path" && error.reason == "missing path",
           "an entry without a path is refused");

    platform::linux::Map unknown;
    const mm::test::scoped_file odd{"mm_linux_fs_odd.mdy", "directory.0.size = 1\n"};
    expect(platform::linux::apply_override(unknown, odd.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "an unknown directory field is refused");
}

void conformance_passes_on_a_host_directory() {
    clear();
    const mm::test::scoped_tree tree{"linux_fs_conformance"};
    expect(mm::fs::native::mount("/host", tree.root().string()) == Status::Ok,
           "a temporary directory mounts");
    mm::fs::conformance::Report report;
    expect(mm::fs::conformance::run("/host", report) == Status::Ok, "the checks run");
    expect(report.failed == 0 && report.passed == 25,
           std::string{"every check passes on Linux; first failure: "} +
               std::string{report.first_failure});
    expect(mm::fs::native::unmount("/host") == Status::Ok, "the directory unmounts");
}

void files_are_the_hosts_files() {
    clear();
    const mm::test::scoped_tree tree{"linux_fs_files"};
    write_host_file(tree.root() / "from-host.txt", "written by the host");
    expect(mm::fs::native::mount("/host", tree.root().string()) == Status::Ok, "it mounts");

    mm::fs::File file;
    expect(mm::fs::open("/host/from-host.txt", Access::Read, Disposition::OpenExisting, file) ==
               Status::Ok,
           "a file the host wrote opens");
    std::array<std::byte, 64> buffer{};
    std::size_t count = 0;
    expect(file.read(buffer, count) == Status::Ok && count == 19, "and reads in full");
    expect(file.close() == Status::Ok, "it closes");

    expect(mm::fs::open("/host/from-mm.txt", Access::Write, Disposition::CreateNew, file) ==
               Status::Ok &&
               file.write(bytes("written through mm.fs"), count) == Status::Ok &&
               file.close() == Status::Ok,
           "a file is written through mm.fs");
    expect(read_host_file(tree.root() / "from-mm.txt") == "written through mm.fs",
           "and the host reads what was written");

    mm::fs::Stat stat;
    expect(mm::fs::stat("/host/from-mm.txt", stat) == Status::Ok && stat.modified > 0,
           "a host file carries the kernel's timestamp");
    expect(mm::fs::native::unmount("/host") == Status::Ok, "it unmounts");
}

void symlinks_stay_inside_the_root() {
    clear();
    const mm::test::scoped_tree tree{"linux_fs_links"};
    const mm::test::scoped_tree outside{"linux_fs_outside"};
    write_host_file(outside.root() / "secret.txt", "outside");
    write_host_file(tree.root() / "inside.txt", "inside");
    std::error_code error;
    std::filesystem::create_directory_symlink(outside.root(), tree.root() / "escape", error);
    std::filesystem::create_symlink(tree.root() / "inside.txt", tree.root() / "alias", error);
    expect(!error, "the fixture's links are made");

    expect(mm::fs::native::mount("/host", tree.root().string()) == Status::Ok, "it mounts");
    mm::fs::Stat stat;
    expect(mm::fs::stat("/host/escape", stat) == Status::NotFound &&
               mm::fs::stat("/host/escape/secret.txt", stat) == Status::NotFound,
           "a link that leaves the root is absent");
    mm::fs::File file;
    expect(mm::fs::open("/host/escape/new.txt", Access::Write, Disposition::CreateNew, file) ==
               Status::NotFound,
           "nothing can be made through it");
    expect(mm::fs::stat("/host/alias", stat) == Status::Ok && stat.size == 6,
           "a link that stays inside the root works");
    expect(mm::fs::native::unmount("/host") == Status::Ok, "it unmounts");

    mm::fs::native::Options open_links;
    open_links.contain_symlinks = false;
    expect(mm::fs::native::mount("/host", tree.root().string(), open_links) == Status::Ok,
           "it mounts with containment off");
    expect(mm::fs::stat("/host/escape/secret.txt", stat) == Status::Ok,
           "and then the link is followed");
    expect(mm::fs::native::unmount("/host") == Status::Ok, "it unmounts");
}

void a_read_only_volume_refuses_writes() {
    clear();
    const mm::test::scoped_tree tree{"linux_fs_read_only"};
    write_host_file(tree.root() / "kept.txt", "kept");
    std::error_code error;
    std::filesystem::create_directory(tree.root() / "folder", error);
    mm::fs::native::Options read_only;
    read_only.read_only = true;
    expect(mm::fs::native::mount("/ro", tree.root().string(), read_only) == Status::Ok,
           "it mounts read-only");

    mm::fs::File file;
    expect(mm::fs::open("/ro/kept.txt", Access::Read, Disposition::OpenExisting, file) ==
               Status::Ok &&
               file.close() == Status::Ok,
           "a file still opens for reading");
    expect(mm::fs::open("/ro/kept.txt", Access::Write, Disposition::OpenExisting, file) ==
               Status::ReadOnly,
           "but not for writing");
    expect(mm::fs::open("/ro/new.txt", Access::Read, Disposition::OpenOrCreate, file) ==
               Status::ReadOnly,
           "nor can one be created");
    expect(mm::fs::make_directory("/ro/made") == Status::ReadOnly &&
               mm::fs::remove("/ro/kept.txt") == Status::ReadOnly &&
               mm::fs::rename("/ro/kept.txt", "/ro/moved.txt") == Status::ReadOnly,
           "make_directory, remove, and rename are ReadOnly");
    mm::fs::Stat stat;
    expect(mm::fs::stat("/ro/kept.txt", stat) == Status::Ok && stat.read_only,
           "stat reports the volume read-only");
    expect(read_host_file(tree.root() / "kept.txt") == "kept", "the host file is untouched");
    expect(mm::fs::native::unmount("/ro") == Status::Ok, "it unmounts");
}

void mounting_checks_the_root() {
    clear();
    const mm::test::scoped_tree tree{"linux_fs_roots"};
    write_host_file(tree.root() / "plain.txt", "x");
    expect(mm::fs::native::mount("/x", "relative/dir") == Status::BadArgument,
           "a relative root is refused");
    expect(mm::fs::native::mount("/x", (tree.root() / "missing").string()) == Status::NotFound,
           "a missing root is NotFound");
    expect(mm::fs::native::mount("/x", (tree.root() / "plain.txt").string()) ==
               Status::NotDirectory,
           "a file as root is NotDirectory");
    expect(mm::fs::native::unmount("/x") == Status::NotFound,
           "and nothing was left mounted");
}

void the_local_storage_is_the_working_directory() {
    clear();
    // The suite's map, platform.linux.defaults with no override, names no
    // directory, so the working directory is the board's storage.
    expect(mm::fs::local::mount("/data") == Status::Ok, "the local storage mounts");
    mm::fs::Stat stat;
    expect(mm::fs::stat("/data", stat) == Status::Ok && stat.kind == mm::fs::Kind::Directory,
           "it is a directory");
    std::error_code error;
    const auto here = std::filesystem::current_path(error);
    expect(mm::fs::stat("/data/" + std::filesystem::directory_iterator{here, error}
                                       ->path()
                                       .filename()
                                       .string(),
                        stat) == Status::Ok,
           "and holds the working directory's entries");
    expect(mm::fs::native::unmount("/data") == Status::BadArgument,
           "mm.fs.native will not unmount what mm.fs.local mounted");
    expect(mm::fs::local::unmount("/data") == Status::Ok, "it unmounts");
}

void errors_map_to_status() {
    using platform::linux::fs_testing::status_of;
    const auto code = [](std::errc value) { return std::make_error_code(value); };
    expect(status_of({}) == Status::Ok, "no error is Ok");
    expect(status_of(code(std::errc::no_such_file_or_directory)) == Status::NotFound &&
               status_of(code(std::errc::file_exists)) == Status::Exists &&
               status_of(code(std::errc::not_a_directory)) == Status::NotDirectory &&
               status_of(code(std::errc::is_a_directory)) == Status::IsDirectory &&
               status_of(code(std::errc::directory_not_empty)) == Status::NotEmpty,
           "the path errors map one for one");
    expect(status_of(code(std::errc::no_space_on_device)) == Status::NoSpace &&
               status_of(code(std::errc::read_only_file_system)) == Status::ReadOnly &&
               status_of(code(std::errc::permission_denied)) == Status::ReadOnly &&
               status_of(code(std::errc::operation_not_permitted)) == Status::ReadOnly,
           "space and permission errors map");
    expect(status_of(code(std::errc::filename_too_long)) == Status::NameTooLong &&
               status_of(code(std::errc::too_many_files_open)) == Status::TooMany &&
               status_of(code(std::errc::too_many_files_open_in_system)) == Status::TooMany &&
               status_of(code(std::errc::device_or_resource_busy)) == Status::Busy &&
               status_of(code(std::errc::cross_device_link)) == Status::CrossVolume,
           "limits, busy, and cross-device errors map");
    expect(status_of(code(std::errc::io_error)) == Status::TransportError,
           "anything else is TransportError");
}

void the_flash_region_is_an_image_file() {
    const mm::test::scoped_tree tree{"linux_fs_flash"};
    const auto image = tree.root() / "flash.img";
    platform::linux::mcu_detail::StorageTestHooks hooks;
    hooks.flash = platform::linux::FlashEntry{image.string(), 16384, 4096, 256};
    platform::linux::mcu_detail::set_storage_test_hooks(&hooks);

    mm::mcu::FlashRegionGeometry geometry;
    expect(mm::mcu::flash_region_geometry(geometry) == mm::mcu::Status::Ok &&
               geometry.size == 16384 && geometry.read_size == 1 &&
               geometry.program_size == 256 && geometry.erase_size == 4096,
           "a new image reports the configured geometry");
    std::error_code error;
    expect(std::filesystem::file_size(image, error) == 16384, "and is made at its size");

    std::vector<std::byte> read(256);
    expect(mm::mcu::flash_region_read(0, read) == mm::mcu::Status::Ok &&
               std::all_of(read.begin(), read.end(),
                           [](std::byte b) { return b == std::byte{0xff}; }),
           "a new image reads erased");

    std::vector<std::byte> page(256, std::byte{0x5a});
    expect(mm::mcu::flash_region_program(4096, page) == mm::mcu::Status::Ok,
           "an erased page programs");
    expect(mm::mcu::flash_region_read(4096, read) == mm::mcu::Status::Ok && read == page,
           "and reads back");
    expect(mm::mcu::flash_region_program(4096, page) == mm::mcu::Status::BadArgument,
           "programming bytes that are not erased is refused");
    expect(mm::mcu::flash_region_program(100, page) == mm::mcu::Status::BadArgument &&
               mm::mcu::flash_region_program(0, std::span{page}.first(100)) ==
                   mm::mcu::Status::BadArgument &&
               mm::mcu::flash_region_program(16384, page) == mm::mcu::Status::BadArgument,
           "a misaligned, partial, or out-of-range program is refused");
    expect(mm::mcu::flash_region_erase(100, 4096) == mm::mcu::Status::BadArgument &&
               mm::mcu::flash_region_erase(0, 100) == mm::mcu::Status::BadArgument &&
               mm::mcu::flash_region_erase(12288, 8192) == mm::mcu::Status::BadArgument,
           "a misaligned or out-of-range erase is refused");
    expect(mm::mcu::flash_region_erase(4096, 4096) == mm::mcu::Status::Ok &&
               mm::mcu::flash_region_read(4096, read) == mm::mcu::Status::Ok &&
               std::all_of(read.begin(), read.end(),
                           [](std::byte b) { return b == std::byte{0xff}; }),
           "an erase makes the block read erased again");
    expect(mm::mcu::flash_region_program(4096, page) == mm::mcu::Status::Ok,
           "and lets it be programmed again");

    std::ifstream in(image, std::ios::binary);
    in.seekg(4096);
    char first = 0;
    in.get(first);
    expect(static_cast<unsigned char>(first) == 0x5a, "the program reached the file");

    mm::fs::McuFlash flash;
    mm::fs::FlashGeometry device;
    expect(flash.geometry(device) == Status::Ok && device.erase_count == 4 &&
               device.erase_size == 4096 && device.program_size == 256,
           "McuFlash presents the region as four erase blocks");

    hooks.flash = platform::linux::FlashEntry{};
    expect(mm::mcu::flash_region_geometry(geometry) == mm::mcu::Status::Unsupported &&
               flash.geometry(device) == Status::Unsupported,
           "with no image named the region is Unsupported");
    platform::linux::mcu_detail::set_storage_test_hooks(nullptr);
}

void map_flash_keys_parse_and_validate() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    const mm::test::scoped_file good{"mm_linux_flash_good.mdy",
                                     "flash.path = \"/srv/flash.img\"\n"
                                     "flash.size = 524288\n"
                                     "flash.erase-size = 4096\n"
                                     "flash.program-size = 256\n"};
    expect(platform::linux::apply_override(map, good.path().string(), error) ==
               platform::linux::MapStatus::Ok &&
               map.flash.path == "/srv/flash.img" && map.flash.size == 524288,
           "valid flash keys parse");
    platform::linux::Map relative;
    const mm::test::scoped_file rel{"mm_linux_flash_rel.mdy", "flash.path = \"flash.img\"\n"};
    expect(platform::linux::apply_override(relative, rel.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "flash.path",
           "a relative image path is refused");
    platform::linux::Map ragged;
    const mm::test::scoped_file odd{"mm_linux_flash_odd.mdy",
                                    "flash.path = \"/srv/flash.img\"\nflash.size = 5000\n"};
    expect(platform::linux::apply_override(ragged, odd.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "flash.size",
           "a size that is not whole erase blocks is refused");
    platform::linux::Map uneven;
    const mm::test::scoped_file mixed{"mm_linux_flash_mixed.mdy",
                                      "flash.erase-size = 4096\nflash.program-size = 3000\n"};
    expect(platform::linux::apply_override(uneven, mixed.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "flash.erase-size",
           "an erase block that is not whole program units is refused");
}

const mm::test::case_ cases[] = {
    {"map directory keys parse and validate", &map_directory_keys_parse_and_validate},
    {"conformance passes on a host directory", &conformance_passes_on_a_host_directory},
    {"files are the host's files", &files_are_the_hosts_files},
    {"symlinks stay inside the root", &symlinks_stay_inside_the_root},
    {"a read-only volume refuses writes", &a_read_only_volume_refuses_writes},
    {"mounting checks the root", &mounting_checks_the_root},
    {"the local storage is the working directory", &the_local_storage_is_the_working_directory},
    {"errors map to Status", &errors_map_to_status},
    {"the flash region is an image file", &the_flash_region_is_an_image_file},
    {"map flash keys parse and validate", &map_flash_keys_parse_and_validate},
};

const mm::test::registrar reg{"platform.linux.fs", cases};

}  // namespace
