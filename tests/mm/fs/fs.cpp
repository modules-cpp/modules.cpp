// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

import mm.fs;
import mm.fs.conformance;
import mm.fs.littlefs;
import mm.fs.local;
import mm.fs.native;
import mm.test;

mm::fs::Volume& mm_test_fs_memory(unsigned int which);
void mm_test_fs_memory_reset();
void mm_test_fs_memory_capacity(unsigned int which, std::uint64_t capacity);
unsigned int mm_test_fs_memory_flushes(unsigned int which);
std::uint64_t mm_test_fs_memory_modified(unsigned int which, const std::string& path);

namespace {

using mm::fs::Access;
using mm::fs::Disposition;
using mm::fs::Status;
using mm::test::expect;

// Remembers the last volume-relative path it was handed, and can be told to
// fail its flush, which is all the resolution and unmount tests need.
class RecordingVolume final : public mm::fs::Volume {
public:
    Status stat(std::string_view path, mm::fs::Stat& stat) override {
        last = std::string{path};
        stat = {mm::fs::Kind::Directory, 0, 0, false};
        return Status::Ok;
    }
    Status flush() override { return flush_status; }

    std::string last = "<none>";
    Status flush_status = Status::Ok;
};

RecordingVolume recording_root;
RecordingVolume recording_usb;

// Unmounts whatever a test left, so each case starts from an empty table.
void reset() {
    for (const std::string_view prefix : {"/", "/usb", "/mem", "/a", "/b", "/c", "/d", "/e",
                                          "/f", "/g", "/h"})
        static_cast<void>(mm::fs::unmount(prefix));
    mm_test_fs_memory_reset();
    recording_root = RecordingVolume{};
    recording_usb = RecordingVolume{};
    mm::fs::set_clock(nullptr);
}

[[nodiscard]] std::string normalized(std::string_view path, Status& status) {
    std::array<char, mm::fs::max_path> out{};
    std::size_t length = 0;
    status = mm::fs::normalize(path, out, length);
    return status == Status::Ok ? std::string{out.data(), length} : std::string{};
}

[[nodiscard]] bool normalizes_to(std::string_view path, std::string_view expected) {
    Status status = Status::BadArgument;
    return normalized(path, status) == expected && status == Status::Ok;
}

[[nodiscard]] Status normalize_status(std::string_view path) {
    Status status = Status::Ok;
    static_cast<void>(normalized(path, status));
    return status;
}

void normalize_makes_paths_canonical() {
    expect(normalizes_to("/", "/"), "the root is itself");
    expect(normalizes_to("//a//b/", "/a/b"), "repeated and trailing separators collapse");
    expect(normalizes_to("/a/./b/../c", "/a/c"), ". drops and .. removes a component");
    expect(normalizes_to("/..", "/") && normalizes_to("/a/../..", "/"),
           ".. never climbs above the root");
    expect(normalizes_to("/a/b/../../c/./", "/c"), "a mixture resolves left to right");
    expect(normalize_status("") == Status::BadArgument, "an empty path is refused");
    expect(normalize_status("a/b") == Status::BadArgument, "a relative path is refused");
    expect(normalize_status(std::string_view{"/a\0b", 4}) == Status::BadArgument,
           "a NUL is refused");
}

void normalize_enforces_limits() {
    const std::string long_name(mm::fs::max_name + 1, 'n');
    expect(normalize_status("/" + long_name) == Status::NameTooLong,
           "a component over max_name is NameTooLong");
    const std::string longest_name(mm::fs::max_name - 1, 'n');
    expect(normalize_status("/" + longest_name) == Status::Ok,
           "a path of exactly max_path fits");
    expect(normalize_status("/" + longest_name + "/x") == Status::NameTooLong,
           "a result over max_path is NameTooLong");

    std::array<char, 4> small{'?', '?', '?', '?'};
    std::size_t length = 99;
    expect(mm::fs::normalize("/abcdef", small, length) == Status::NameTooLong &&
               length == 99 && small[0] == '?',
           "a result over out is NameTooLong and leaves out and length alone");
    expect(mm::fs::normalize("/abc", small, length) == Status::Ok && length == 4,
           "a result that exactly fills out is Ok");
}

void mount_checks_shapes_and_limits() {
    reset();
    auto& volume = mm_test_fs_memory(0);
    expect(mm::fs::mount("/a/b", volume) == Status::BadArgument,
           "a prefix of two components is refused");
    expect(mm::fs::mount("mem", volume) == Status::BadArgument, "a relative prefix is refused");
    expect(mm::fs::mount("/mem/", volume) == Status::Ok, "a trailing separator is normalised");
    expect(mm::fs::mount("/mem", mm_test_fs_memory(1)) == Status::Exists,
           "a mounted prefix is Exists");
    expect(mm::fs::mount("/other", volume) == Status::Busy,
           "a volume mounted elsewhere is Busy");
    expect(mm::fs::unmount("/nowhere") == Status::NotFound, "an unmounted prefix is NotFound");
    expect(mm::fs::unmount("/mem") == Status::Ok && mm_test_fs_memory_flushes(0) == 1,
           "unmount flushes the volume");

    std::array<RecordingVolume, mm::fs::max_mounts + 1> many;
    const std::array<std::string_view, mm::fs::max_mounts + 1> prefixes{
        "/a", "/b", "/c", "/d", "/e", "/f", "/g", "/h", "/usb"};
    for (std::size_t i = 0; i < mm::fs::max_mounts; ++i)
        expect(mm::fs::mount(prefixes[i], many[i]) == Status::Ok, "a free slot mounts");
    expect(mm::fs::mount(prefixes[mm::fs::max_mounts], many[mm::fs::max_mounts]) ==
               Status::TooMany,
           "a ninth mount is TooMany");
    for (std::size_t i = 0; i < mm::fs::max_mounts; ++i)
        static_cast<void>(mm::fs::unmount(prefixes[i]));
}

void paths_resolve_to_the_longest_prefix() {
    reset();
    mm::fs::Stat stat;
    expect(mm::fs::stat("/usb/a", stat) == Status::NotFound,
           "with nothing mounted every path is NotFound");
    expect(mm::fs::mount("/usb", recording_usb) == Status::Ok, "/usb mounts");
    expect(mm::fs::stat("/elsewhere", stat) == Status::NotFound,
           "a path under no mount is NotFound");
    expect(mm::fs::mount("/", recording_root) == Status::Ok, "/ mounts beside it");

    expect(mm::fs::stat("/usb/a/b", stat) == Status::Ok && recording_usb.last == "a/b",
           "a path under /usb reaches /usb, relative to it");
    expect(mm::fs::stat("/usb", stat) == Status::Ok && recording_usb.last.empty(),
           "the prefix itself is the volume's root");
    expect(mm::fs::stat("/usbstick/x", stat) == Status::Ok &&
               recording_root.last == "usbstick/x",
           "a name that merely starts with the prefix is not under it");
    expect(mm::fs::stat("/usb/../other/./x", stat) == Status::Ok &&
               recording_root.last == "other/x",
           "normalisation happens before resolution");
    expect(mm::fs::stat("/", stat) == Status::Ok && recording_root.last.empty(),
           "/ is the root volume's root");
}

void mm_fs_owns_its_own_rules() {
    reset();
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok &&
               mm::fs::mount("/usb", mm_test_fs_memory(1)) == Status::Ok,
           "two volumes mount");
    mm::fs::File file;
    expect(mm::fs::open("/mem/x", Access::Write, Disposition::CreateNew, file) == Status::Ok,
           "a file is created");
    expect(mm::fs::rename("/mem/x", "/usb/x") == Status::CrossVolume,
           "rename between mounts is CrossVolume");
    expect(mm::fs::remove("/mem") == Status::BadArgument,
           "a mount point cannot be removed");
    expect(mm::fs::rename("/mem", "/mem/y") == Status::BadArgument,
           "a mount point cannot be renamed");
    expect(mm::fs::make_directory("/mem") == Status::Exists, "a mount point exists");

    mm::fs::File again;
    expect(mm::fs::open("/mem/x", Access::Write, Disposition::OpenExisting, file) ==
               Status::BadArgument,
           "opening into an open File is refused rather than closing it silently");
    expect(mm::fs::unmount("/mem") == Status::Busy, "a volume with an open file is Busy");
    expect(file.close() == Status::Ok, "the file closes");
    expect(mm::fs::unmount("/mem") == Status::Ok, "and then the volume unmounts");
    static_cast<void>(again);
}

void a_failed_flush_keeps_the_volume() {
    reset();
    recording_usb.flush_status = Status::TransportError;
    expect(mm::fs::mount("/usb", recording_usb) == Status::Ok, "the volume mounts");
    expect(mm::fs::unmount("/usb") == Status::TransportError,
           "a failed flush is returned");
    mm::fs::Stat stat;
    expect(mm::fs::stat("/usb/x", stat) == Status::Ok, "and the volume stays mounted");
    recording_usb.flush_status = Status::Ok;
    expect(mm::fs::unmount("/usb") == Status::Ok, "it unmounts once the flush succeeds");
}

void files_move_and_close() {
    reset();
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "the volume mounts");
    {
        mm::fs::File original;
        expect(mm::fs::open("/mem/m", Access::Write, Disposition::CreateNew, original) ==
                   Status::Ok,
               "a file opens");
        mm::fs::File moved{std::move(original)};
        expect(moved.is_open() && !original.is_open(), "a move transfers the handle");
        mm::fs::File assigned;
        assigned = std::move(moved);
        expect(assigned.is_open() && !moved.is_open(), "a move assignment transfers it too");
        expect(mm::fs::unmount("/mem") == Status::Busy, "the moved handle still counts");
    }
    expect(mm::fs::unmount("/mem") == Status::Ok,
           "the destructor closed the file, so the volume unmounts");

    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "it mounts again");
    {
        mm::fs::Directory listing;
        expect(mm::fs::open_directory("/mem", listing) == Status::Ok, "a directory opens");
        mm::fs::Directory moved{std::move(listing)};
        expect(moved.is_open() && !listing.is_open(), "a directory moves too");
        expect(mm::fs::unmount("/mem") == Status::Busy, "an open directory counts");
    }
    expect(mm::fs::unmount("/mem") == Status::Ok, "and its destructor closes it");
}

std::uint64_t fixed_clock() { return 1'780'000'000; }

void the_clock_is_installed() {
    reset();
    expect(mm::fs::now() == 0, "with no clock, now is unknown");
    mm::fs::set_clock(&fixed_clock);
    expect(mm::fs::now() == 1'780'000'000, "an installed clock answers");
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "the volume mounts");
    mm::fs::File file;
    expect(mm::fs::open("/mem/stamped", Access::Write, Disposition::CreateNew, file) ==
               Status::Ok &&
               file.close() == Status::Ok,
           "a file is created");
    mm::fs::Stat stat;
    expect(mm::fs::stat("/mem/stamped", stat) == Status::Ok && stat.modified == 1'780'000'000,
           "a volume stamps it with the clock");
    mm::fs::set_clock(nullptr);
    expect(mm::fs::now() == 0, "and removing the clock makes now unknown again");
}

void a_full_volume_is_no_space() {
    reset();
    mm_test_fs_memory_capacity(0, 4);
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "the volume mounts");
    mm::fs::File file;
    expect(mm::fs::open("/mem/big", Access::Write, Disposition::CreateNew, file) == Status::Ok,
           "a file opens");
    const std::array<std::byte, 6> six{};
    std::size_t written = 99;
    expect(file.write(six, written) == Status::NoSpace && written == 4,
           "a write that fills the volume is NoSpace with the count written");
    expect(file.close() == Status::Ok, "the file closes");
}

void mcu_storage_answers_for_the_platform() {
    mm::fs::McuStorage storage;
    mm::fs::BlockGeometry geometry{7, 7};
    std::array<std::byte, 512> block{};
    expect(storage.geometry(geometry) == Status::Unsupported && geometry.count == 7,
           "with no storage facility, geometry is Unsupported and untouched");
    expect(storage.read(0, block) == Status::Unsupported, "and so is a read");
    expect(storage.sync() == Status::Ok, "sync has nothing to do");

    mm::fs::McuFlash flash;
    mm::fs::FlashGeometry flash_geometry{1, 2, 3, 4};
    expect(flash.geometry(flash_geometry) == Status::Unsupported && flash_geometry.erase_count == 4,
           "with no flash region, McuFlash's geometry is Unsupported and untouched");
    expect(flash.erase(0, 4096) == Status::Unsupported, "and so is an erase");
}

void conformance_passes_on_the_memory_volume() {
    reset();
    expect(mm::fs::mount("/", mm_test_fs_memory(0)) == Status::Ok, "the volume mounts at /");
    mm::fs::conformance::Report report;
    expect(mm::fs::conformance::run("/", report) == Status::Ok, "the checks run at /");
    expect(report.failed == 0 && report.passed == 25 && report.first_failure.empty(),
           "every check passes at /");
    mm::fs::Stat stat;
    expect(mm::fs::stat("/mm-fs-conformance", stat) == Status::NotFound,
           "the scratch directory is gone afterwards");

    expect(mm::fs::mount("/mem", mm_test_fs_memory(1)) == Status::Ok, "a second volume mounts");
    mm::fs::conformance::Report under;
    expect(mm::fs::conformance::run("/mem", under) == Status::Ok && under.failed == 0 &&
               under.passed == 25,
           "every check passes under a prefix too");
}

void conformance_reports_a_failing_volume() {
    reset();
    // A volume that cannot make directories cannot host the scratch area.
    expect(mm::fs::mount("/usb", recording_usb) == Status::Ok, "the volume mounts");
    mm::fs::conformance::Report report{3, 4, "untouched"};
    expect(mm::fs::conformance::run("/usb", report) == Status::Unsupported &&
               report.passed == 3 && report.first_failure == "untouched",
           "a scratch directory that cannot be made is reported, report untouched");
}

// A provider that hands out the second memory volume and records what it was
// asked, for both mount interfaces.
struct RecordingLocal final : mm::fs::local::Provider {
    Status attach(const mm::fs::local::Options& options, mm::fs::Volume*& volume) override {
        ++attaches;
        read_only = options.read_only;
        volume = &mm_test_fs_memory(1);
        return Status::Ok;
    }
    Status detach(mm::fs::Volume& volume) override {
        ++detaches;
        return &volume == &mm_test_fs_memory(1) ? Status::Ok : Status::BadArgument;
    }
    unsigned int attaches = 0;
    unsigned int detaches = 0;
    bool read_only = false;
};

struct RecordingNative final : mm::fs::native::Provider {
    Status attach(std::string_view root, const mm::fs::native::Options&,
                  mm::fs::Volume*& volume) override {
        last_root = std::string{root};
        volume = &mm_test_fs_memory(1);
        return Status::Ok;
    }
    Status detach(mm::fs::Volume&) override {
        ++detaches;
        return Status::Ok;
    }
    std::string last_root;
    unsigned int detaches = 0;
};

void mount_interfaces_fall_back_to_unsupported() {
    reset();
    // Nothing in this binary binds a provider, so the fallbacks answer.
    expect(mm::fs::local::mount("/data") == Status::Unsupported,
           "mm.fs.local without a provider is Unsupported");
    expect(mm::fs::native::mount("/host", "/tmp") == Status::Unsupported,
           "mm.fs.native without a provider is Unsupported");
    mm::fs::Volume* volume = nullptr;
    expect(mm::fs::mounted("/data", volume) == Status::NotFound && volume == nullptr,
           "and nothing was mounted");
}

void mount_interfaces_attach_publish_and_detach() {
    reset();
    RecordingLocal local;
    RecordingNative native;
    mm::fs::local::set_provider(local);
    mm::fs::native::set_provider(native);

    mm::fs::local::Options options;
    options.read_only = true;
    expect(mm::fs::local::mount("/data", options) == Status::Ok && local.attaches == 1 &&
               local.read_only,
           "local mount attaches with the caller's options");
    mm::fs::Volume* volume = nullptr;
    expect(mm::fs::mounted("/data", volume) == Status::Ok && volume == &mm_test_fs_memory(1),
           "and publishes the attached volume");
    expect(mm::fs::local::mount("/data") == Status::Exists && local.detaches == 1,
           "a failed publish detaches what it attached");
    expect(mm::fs::native::unmount("/data") == Status::BadArgument,
           "another interface will not unmount it");
    expect(mm::fs::local::unmount("/data") == Status::Ok && local.detaches == 2,
           "local unmount detaches");

    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "a volume mounts directly");
    expect(mm::fs::local::unmount("/mem") == Status::BadArgument,
           "local will not unmount a volume it did not attach");
    expect(mm::fs::unmount("/mem") == Status::Ok, "mm.fs unmounts it");

    expect(mm::fs::native::mount("/host", "/srv/export") == Status::Ok &&
               native.last_root == "/srv/export",
           "native mount hands the root to the provider");
    expect(mm::fs::native::unmount("/host") == Status::Ok && native.detaches == 1,
           "native unmount detaches");

    mm::fs::local::Provider fallback_local;
    mm::fs::native::Provider fallback_native;
    mm::fs::local::set_provider(fallback_local);
    mm::fs::native::set_provider(fallback_native);
}

struct RecordingLittlefs final : mm::fs::littlefs::Provider {
    Status attach(mm::fs::FlashDevice& device, const mm::fs::littlefs::Options& options,
                  mm::fs::Volume*& volume) override {
        last_device = &device;
        last_cycles = options.block_cycles;
        volume = &mm_test_fs_memory(1);
        return Status::Ok;
    }
    Status detach(mm::fs::Volume&) override {
        ++detaches;
        return Status::Ok;
    }
    Status format(mm::fs::FlashDevice& device) override {
        formatted = &device;
        return Status::Ok;
    }
    mm::fs::FlashDevice* last_device = nullptr;
    mm::fs::FlashDevice* formatted = nullptr;
    std::int32_t last_cycles = 0;
    unsigned int detaches = 0;
};

void littlefs_interface_forwards_to_its_provider() {
    reset();
    mm::fs::FlashDevice device;
    expect(mm::fs::littlefs::mount("/flash", device) == Status::Unsupported &&
               mm::fs::littlefs::format(device) == Status::Unsupported,
           "with no provider, littlefs mount and format are Unsupported");

    RecordingLittlefs recording;
    mm::fs::littlefs::set_provider(recording);
    mm::fs::littlefs::Options options;
    options.block_cycles = 123;
    expect(mm::fs::littlefs::mount("/flash", device, options) == Status::Ok &&
               recording.last_device == &device && recording.last_cycles == 123,
           "mount hands the device and options to the provider");
    expect(mm::fs::local::unmount("/flash") == Status::BadArgument,
           "another interface will not unmount it");
    expect(mm::fs::littlefs::unmount("/flash") == Status::Ok && recording.detaches == 1,
           "unmount detaches");
    expect(mm::fs::littlefs::format(device) == Status::Ok && recording.formatted == &device,
           "format forwards the device");
    mm::fs::littlefs::Provider fallback;
    mm::fs::littlefs::set_provider(fallback);
}

const mm::test::case_ cases[] = {
    {"normalize makes paths canonical", &normalize_makes_paths_canonical},
    {"normalize enforces limits", &normalize_enforces_limits},
    {"mount checks shapes and limits", &mount_checks_shapes_and_limits},
    {"paths resolve to the longest prefix", &paths_resolve_to_the_longest_prefix},
    {"mm.fs owns its own rules", &mm_fs_owns_its_own_rules},
    {"a failed flush keeps the volume", &a_failed_flush_keeps_the_volume},
    {"files and directories move and close", &files_move_and_close},
    {"the clock is installed", &the_clock_is_installed},
    {"a full volume is NoSpace", &a_full_volume_is_no_space},
    {"McuStorage answers for the platform", &mcu_storage_answers_for_the_platform},
    {"conformance passes on the memory volume", &conformance_passes_on_the_memory_volume},
    {"conformance reports a failing volume", &conformance_reports_a_failing_volume},
    {"mount interfaces fall back to Unsupported", &mount_interfaces_fall_back_to_unsupported},
    {"mount interfaces attach, publish, and detach", &mount_interfaces_attach_publish_and_detach},
    {"the littlefs interface forwards to its provider", &littlefs_interface_forwards_to_its_provider},
};

const mm::test::registrar reg{"mm.fs", cases};

}  // namespace
