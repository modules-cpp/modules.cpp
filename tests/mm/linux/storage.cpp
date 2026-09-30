// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import mm.mcu;
import mm.test;
import platform.linux.map;
import platform.linux.defaults;
import platform.linux.mcu;

namespace {

using mm::mcu::Status;
using mm::test::expect;

const platform::linux::Map default_map{};

void map_keys_parse_and_validate() {
    platform::linux::Map map;
    platform::linux::ParseError error;
    const mm::test::scoped_file good{
        "mm_linux_storage_good.mdy",
        "storage.0.path = \"/dev/sda\"\n"
        "storage.0.writable = yes\n"
        "storage.1.path = \"/dev/sdb\"\n"};
    expect(platform::linux::apply_override(map, good.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "valid storage keys parse");
    expect(map.storages.size() == 2, "two storage entries parsed");
    expect(map.storages[0].path == "/dev/sda" && map.storages[0].writable,
           "entry 0 has path and writable yes");
    expect(map.storages[1].path == "/dev/sdb" && !map.storages[1].writable,
           "entry 1 defaults writable to no");

    platform::linux::Map bad_rel;
    const mm::test::scoped_file rel{
        "mm_linux_storage_rel.mdy",
        "storage.0.path = \"dev/sda\"\n"};
    expect(platform::linux::apply_override(bad_rel, rel.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "storage.0.path" && error.reason == "relative path",
           "relative path is refused");

    platform::linux::Map bad_writable;
    const mm::test::scoped_file writable{
        "mm_linux_storage_writable.mdy",
        "storage.0.writable = yes\n"};
    expect(platform::linux::apply_override(bad_writable, writable.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError &&
               error.key == "storage.0.writable" && error.reason == "writable without a path",
           "writable without a path is refused");

    platform::linux::Map bad_unknown;
    const mm::test::scoped_file unknown{
        "mm_linux_storage_unknown.mdy",
        "storage.0.foo = 1\n"};
    expect(platform::linux::apply_override(bad_unknown, unknown.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "unknown storage key is refused");
}

void inspect_sysfs_block_and_mountinfo() {
    using platform::linux::mcu_detail::inspect_sysfs_block;
    using platform::linux::mcu_detail::mountinfo_has_device;
    using platform::linux::mcu_detail::BlockDiskInfo;

    const mm::test::scoped_tree tree{"storage_sysfs"};
    const auto sysfs_root = tree.root();

    // Create sda whole disk with sda1 partition
    const auto sda_dir = sysfs_root / "sda";
    std::filesystem::create_directories(sda_dir / "holders");
    {
        std::ofstream dev_out(sda_dir / "dev");
        dev_out << "8:0\n";
    }
    const auto sda1_dir = sda_dir / "sda1";
    std::filesystem::create_directories(sda1_dir / "holders");
    {
        std::ofstream part_out(sda1_dir / "partition");
        part_out << "1\n";
        std::ofstream dev_out(sda1_dir / "dev");
        dev_out << "8:1\n";
    }

    BlockDiskInfo info;
    expect(inspect_sysfs_block(sysfs_root.string(), "sda", info),
           "inspect_sysfs_block succeeds on sda");
    expect(info.is_whole_disk, "sda is detected as whole disk");
    expect(!info.has_holders, "sda has no holders");
    expect(info.dev_id == "8:0", "sda dev_id matches 8:0");
    expect(info.partition_names.size() == 1 && info.partition_names[0] == "sda1",
           "sda partition sda1 discovered");
    expect(info.partition_dev_ids.size() == 1 && info.partition_dev_ids[0] == "8:1",
           "sda1 dev_id matches 8:1");

    BlockDiskInfo part_info;
    expect(inspect_sysfs_block(sysfs_root.string(), "sda/sda1", part_info),
           "inspect_sysfs_block succeeds on partition");
    expect(!part_info.is_whole_disk, "sda1 is detected as partition");
    expect(part_info.dev_id == "8:1", "sda1 dev_id matches 8:1");

    // Create sdb whole disk with holders
    const auto sdb_dir = sysfs_root / "sdb";
    std::filesystem::create_directories(sdb_dir / "holders");
    {
        std::ofstream dev_out(sdb_dir / "dev");
        dev_out << "8:16\n";
        std::ofstream holder_out(sdb_dir / "holders" / "dm-0");
        holder_out << "holder\n";
    }
    BlockDiskInfo sdb_info;
    expect(inspect_sysfs_block(sysfs_root.string(), "sdb", sdb_info),
           "inspect_sysfs_block succeeds on sdb");
    expect(sdb_info.is_whole_disk && sdb_info.has_holders,
           "sdb detected as whole disk with holders");

    // Create sdc whole disk whose partition has holders
    const auto sdc_dir = sysfs_root / "sdc";
    std::filesystem::create_directories(sdc_dir / "holders");
    {
        std::ofstream dev_out(sdc_dir / "dev");
        dev_out << "8:32\n";
    }
    const auto sdc1_dir = sdc_dir / "sdc1";
    std::filesystem::create_directories(sdc1_dir / "holders");
    {
        std::ofstream part_out(sdc1_dir / "partition");
        part_out << "1\n";
        std::ofstream dev_out(sdc1_dir / "dev");
        dev_out << "8:33\n";
        std::ofstream holder_out(sdc1_dir / "holders" / "dm-1");
        holder_out << "holder\n";
    }
    BlockDiskInfo sdc_info;
    expect(inspect_sysfs_block(sysfs_root.string(), "sdc", sdc_info),
           "inspect_sysfs_block succeeds on sdc");
    expect(sdc_info.is_whole_disk && sdc_info.has_holders,
           "sdc detected as having holders because sdc1 has holders");

    BlockDiskInfo none_info;
    expect(!inspect_sysfs_block(sysfs_root.string(), "nonexistent", none_info),
           "nonexistent block device returns false");

    // Test mountinfo_has_device
    const std::string mountinfo =
        "34 2 259:5 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p5 rw\n"
        "40 20 8:1 /mnt/usb rw,relatime shared:2 - vfat /dev/sda1 rw\n";

    expect(mountinfo_has_device(mountinfo, "8:0", {"8:1"}, "/dev/sda", {"/dev/sda1"}),
           "matches disk whose partition dev_id is mounted");
    expect(mountinfo_has_device(mountinfo, "259:5", {}, "/dev/nvme0n1p5", {}),
           "matches directly mounted device");
    expect(mountinfo_has_device(mountinfo, "", {}, "", {"/dev/sda1"}),
           "matches mounted partition path");
    expect(!mountinfo_has_device(mountinfo, "8:16", {"8:17"}, "/dev/sdb", {"/dev/sdb1"}),
           "unmounted device does not match mountinfo");
}

void provider_without_storage_answers_unsupported() {
    platform::linux::set_map(default_map);
    expect(!mm::mcu::capabilities().storage,
           "storage capability is false when unconfigured");
    bool present = true;
    expect(mm::mcu::storage_poll(present) == mm::mcu::Status::Unsupported && present,
           "storage_poll returns Unsupported");
    mm::mcu::StorageGeometry geom{};
    expect(mm::mcu::storage_geometry(geom) == mm::mcu::Status::Unsupported,
           "storage_geometry returns Unsupported");
    std::array<std::byte, 512> buf{};
    expect(mm::mcu::storage_read(0, buf) == mm::mcu::Status::Unsupported,
           "storage_read returns Unsupported");
    expect(mm::mcu::storage_write(0, buf) == mm::mcu::Status::Unsupported,
           "storage_write returns Unsupported");
}

void storage_read_and_write_with_test_hooks() {
    const mm::test::scoped_tree tree{"storage_rw"};
    const auto disk_path = tree.root() / "testdisk";

    // Create 2048-byte file (4 x 512 bytes)
    std::vector<std::byte> initial_data(2048);
    for (std::size_t i = 0; i < 512; ++i) initial_data[i] = std::byte{0xAA};
    for (std::size_t i = 512; i < 1024; ++i) initial_data[i] = std::byte{0xBB};
    for (std::size_t i = 1024; i < 1536; ++i) initial_data[i] = std::byte{0xCC};
    for (std::size_t i = 1536; i < 2048; ++i) initial_data[i] = std::byte{0xDD};

    {
        std::ofstream out(disk_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(initial_data.data()), initial_data.size());
    }

    platform::linux::Map test_map;
    test_map.storages.push_back({disk_path.string(), true});
    platform::linux::set_map(test_map);

    platform::linux::mcu_detail::StorageTestHooks hooks;
    hooks.allow_regular_file = true;
    hooks.storage_path = disk_path.string();
    hooks.storage_writable = true;
    platform::linux::mcu_detail::set_storage_test_hooks(&hooks);

    expect(mm::mcu::capabilities().storage, "storage capability is true when configured");

    mm::mcu::StorageGeometry geom{};
    expect(mm::mcu::storage_geometry(geom) == mm::mcu::Status::TransportError,
           "storage_geometry before poll returns TransportError");

    bool present = false;
    expect(mm::mcu::storage_poll(present) == mm::mcu::Status::Ok && present,
           "storage_poll discovers regular file and reports present");

    expect(mm::mcu::storage_geometry(geom) == mm::mcu::Status::Ok &&
               geom.block_size == 512 && geom.block_count == 4,
           "storage_geometry reports 512-byte blocks and 4 blocks total");

    // Read block 1
    std::array<std::byte, 512> block1{};
    expect(mm::mcu::storage_read(1, block1) == mm::mcu::Status::Ok,
           "storage_read of block 1 succeeds");
    bool block1_ok = true;
    for (std::byte b : block1) {
        if (b != std::byte{0xBB}) { block1_ok = false; break; }
    }
    expect(block1_ok, "block 1 content matches 0xBB");

    // Read boundary and argument errors
    std::array<std::byte, 500> unaligned_read{};
    expect(mm::mcu::storage_read(0, unaligned_read) == mm::mcu::Status::BadArgument,
           "non-block-aligned read span is BadArgument");
    expect(mm::mcu::storage_read(4, block1) == mm::mcu::Status::BadArgument,
           "read past end of device is BadArgument");
    std::array<std::byte, 1024> two_blocks{};
    expect(mm::mcu::storage_read(3, two_blocks) == mm::mcu::Status::BadArgument,
           "read overflowing block count is BadArgument");

    // Write block 2 with 0x42
    std::array<std::byte, 512> write_pattern{};
    write_pattern.fill(std::byte{0x42});
    expect(mm::mcu::storage_write(2, write_pattern) == mm::mcu::Status::Ok,
           "storage_write of block 2 succeeds");

    // Read back block 2
    std::array<std::byte, 512> readback{};
    expect(mm::mcu::storage_read(2, readback) == mm::mcu::Status::Ok,
           "readback of block 2 succeeds");
    bool write_ok = true;
    for (std::byte b : readback) {
        if (b != std::byte{0x42}) { write_ok = false; break; }
    }
    expect(write_ok, "block 2 readback matches written pattern");

    // Verify neighbor blocks untouched
    std::array<std::byte, 512> check_block{};
    expect(mm::mcu::storage_read(1, check_block) == mm::mcu::Status::Ok &&
               check_block[0] == std::byte{0xBB},
           "block 1 untouched after write to block 2");
    expect(mm::mcu::storage_read(3, check_block) == mm::mcu::Status::Ok &&
               check_block[0] == std::byte{0xDD},
           "block 3 untouched after write to block 2");

    // Write boundary and argument errors
    std::array<std::byte, 500> unaligned_write{};
    expect(mm::mcu::storage_write(0, unaligned_write) == mm::mcu::Status::BadArgument,
           "non-block-aligned write span is BadArgument");
    expect(mm::mcu::storage_write(4, write_pattern) == mm::mcu::Status::BadArgument,
           "write past end of device is BadArgument");

    // Test read-only refusal
    hooks.storage_writable = false;
    expect(mm::mcu::storage_write(2, write_pattern) == mm::mcu::Status::Unsupported,
           "storage_write answers Unsupported when writable = no");

    // Cleanup
    platform::linux::mcu_detail::set_storage_test_hooks(nullptr);
    platform::linux::set_map(default_map);
}

void storage_safety_refusal_rules() {
    const mm::test::scoped_tree tree{"storage_refusal"};
    const auto sysfs_root = tree.root() / "sysfs";
    const auto mountinfo_path = tree.root() / "mountinfo";
    const auto disk_path = tree.root() / "disk_safe";

    std::vector<char> zeros(1024, 0);
    {
        std::ofstream out(disk_path, std::ios::binary);
        out.write(zeros.data(), zeros.size());
    }

    // Setup sysfs for disk_safe: whole disk, no holders
    const auto safe_sysfs = sysfs_root / "disk_safe";
    std::filesystem::create_directories(safe_sysfs / "holders");
    {
        std::ofstream dev_out(safe_sysfs / "dev");
        dev_out << "99:0\n";
    }

    // 1. Refusal when mounted
    {
        std::ofstream mnt(mountinfo_path);
        mnt << "99 2 99:0 /mnt/test rw - ext4 /dev/disk_safe rw\n";
    }
    platform::linux::mcu_detail::StorageTestHooks hooks;
    hooks.allow_regular_file = true;
    hooks.sysfs_block_dir = sysfs_root.string();
    hooks.mountinfo_path = mountinfo_path.string();
    hooks.storage_path = disk_path.string();
    hooks.storage_writable = true;
    platform::linux::mcu_detail::set_storage_test_hooks(&hooks);

    bool present = false;
    expect(mm::mcu::storage_poll(present) == mm::mcu::Status::Ok && present,
           "storage_poll succeeds on mounted device");
    std::array<std::byte, 512> block{};
    expect(mm::mcu::storage_write(0, block) == mm::mcu::Status::Busy,
           "storage_write answers Busy when disk is mounted");

    // 2. Refusal when has holders
    {
        std::ofstream mnt(mountinfo_path);
        mnt << "1 1 0:0 / / rw - tmpfs tmpfs rw\n";
    }
    const auto holder_disk_path = tree.root() / "disk_holders";
    {
        std::ofstream out(holder_disk_path, std::ios::binary);
        out.write(zeros.data(), zeros.size());
    }
    const auto holder_sysfs = sysfs_root / "disk_holders";
    std::filesystem::create_directories(holder_sysfs / "holders");
    {
        std::ofstream dev_out(holder_sysfs / "dev");
        dev_out << "99:1\n";
        std::ofstream h(holder_sysfs / "holders" / "dm-0");
        h << "holder\n";
    }
    hooks.storage_path = holder_disk_path.string();
    present = false;
    expect(mm::mcu::storage_poll(present) == mm::mcu::Status::Ok && present,
           "storage_poll succeeds on held device");
    expect(mm::mcu::storage_write(0, block) == mm::mcu::Status::Busy,
           "storage_write answers Busy when disk has holders");

    // 3. Refusal when partition
    const auto part_disk_path = tree.root() / "disk_part";
    {
        std::ofstream out(part_disk_path, std::ios::binary);
        out.write(zeros.data(), zeros.size());
    }
    const auto part_sysfs = sysfs_root / "disk_part";
    std::filesystem::create_directories(part_sysfs / "holders");
    {
        std::ofstream dev_out(part_sysfs / "dev");
        dev_out << "99:2\n";
        std::ofstream p(part_sysfs / "partition");
        p << "1\n";
    }
    hooks.storage_path = part_disk_path.string();
    present = false;
    expect(mm::mcu::storage_poll(present) == mm::mcu::Status::Ok && present,
           "storage_poll succeeds on partition file under regular test hook");
    expect(mm::mcu::storage_write(0, block) == mm::mcu::Status::BadArgument,
           "storage_write answers BadArgument when target is a partition");

    // Cleanup
    platform::linux::mcu_detail::set_storage_test_hooks(nullptr);
    platform::linux::set_map(default_map);
}

const mm::test::case_ cases[] = {
    {"map keys parse and validate", &map_keys_parse_and_validate},
    {"inspect sysfs block and mountinfo", &inspect_sysfs_block_and_mountinfo},
    {"provider without storage answers unsupported", &provider_without_storage_answers_unsupported},
    {"storage read and write with test hooks", &storage_read_and_write_with_test_hooks},
    {"storage safety refusal rules", &storage_safety_refusal_rules},
};

const mm::test::registrar reg{"platform.linux.mcu storage", cases};

}  // namespace
