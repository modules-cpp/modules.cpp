// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "../../lfs-cxx.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module platform.linux.fs.littlefs;

import mm.fs;
import mm.fs.littlefs;

namespace platform::linux::littlefs_provider {

// Results cross the C boundary as the adapter's integers, numbered as
// mm::fs::Status is; they are mapped by name both ways, never cast.
mm::fs::Status from(int code) {
    using mm::fs::Status;
    switch (code) {
        case MM_LINUX_LFS_OK: return Status::Ok;
        case MM_LINUX_LFS_BAD_ARGUMENT: return Status::BadArgument;
        case MM_LINUX_LFS_UNSUPPORTED: return Status::Unsupported;
        case MM_LINUX_LFS_NOT_FOUND: return Status::NotFound;
        case MM_LINUX_LFS_EXISTS: return Status::Exists;
        case MM_LINUX_LFS_NOT_DIRECTORY: return Status::NotDirectory;
        case MM_LINUX_LFS_IS_DIRECTORY: return Status::IsDirectory;
        case MM_LINUX_LFS_NOT_EMPTY: return Status::NotEmpty;
        case MM_LINUX_LFS_NO_SPACE: return Status::NoSpace;
        case MM_LINUX_LFS_READ_ONLY: return Status::ReadOnly;
        case MM_LINUX_LFS_NAME_TOO_LONG: return Status::NameTooLong;
        case MM_LINUX_LFS_TOO_MANY: return Status::TooMany;
        case MM_LINUX_LFS_BUSY: return Status::Busy;
        case MM_LINUX_LFS_CROSS_VOLUME: return Status::CrossVolume;
        case MM_LINUX_LFS_CORRUPT: return Status::Corrupt;
        case MM_LINUX_LFS_TIMEOUT: return Status::Timeout;
        default: return Status::TransportError;
    }
}

namespace {

[[nodiscard]] int to(mm::fs::Status status) {
    using mm::fs::Status;
    switch (status) {
        case Status::Ok: return MM_LINUX_LFS_OK;
        case Status::BadArgument: return MM_LINUX_LFS_BAD_ARGUMENT;
        case Status::Unsupported: return MM_LINUX_LFS_UNSUPPORTED;
        case Status::NotFound: return MM_LINUX_LFS_NOT_FOUND;
        case Status::Exists: return MM_LINUX_LFS_EXISTS;
        case Status::NotDirectory: return MM_LINUX_LFS_NOT_DIRECTORY;
        case Status::IsDirectory: return MM_LINUX_LFS_IS_DIRECTORY;
        case Status::NotEmpty: return MM_LINUX_LFS_NOT_EMPTY;
        case Status::NoSpace: return MM_LINUX_LFS_NO_SPACE;
        case Status::ReadOnly: return MM_LINUX_LFS_READ_ONLY;
        case Status::NameTooLong: return MM_LINUX_LFS_NAME_TOO_LONG;
        case Status::TooMany: return MM_LINUX_LFS_TOO_MANY;
        case Status::Busy: return MM_LINUX_LFS_BUSY;
        case Status::CrossVolume: return MM_LINUX_LFS_CROSS_VOLUME;
        case Status::Corrupt: return MM_LINUX_LFS_CORRUPT;
        case Status::Timeout: return MM_LINUX_LFS_TIMEOUT;
        case Status::TransportError: return MM_LINUX_LFS_TRANSPORT_ERROR;
    }
    return MM_LINUX_LFS_TRANSPORT_ERROR;
}

[[nodiscard]] mm::fs::Stat stat_of(const mm_linux_lfs_entry& entry) {
    return {entry.directory != 0 ? mm::fs::Kind::Directory : mm::fs::Kind::File, entry.size,
            entry.modified, entry.read_only != 0};
}

}  // namespace

}  // namespace platform::linux::littlefs_provider

// The adapter calls these, so they have C language linkage; each casts its
// context back to the FlashDevice the record was made for.
extern "C" {

int mm_linux_lfs_provider_geometry(void* context, uint32_t* read_size, uint32_t* program_size,
                                  uint32_t* erase_size, uint32_t* erase_count) {
    auto& device = *static_cast<mm::fs::FlashDevice*>(context);
    mm::fs::FlashGeometry geometry;
    const auto status = device.geometry(geometry);
    if (status == mm::fs::Status::Ok) {
        *read_size = geometry.read_size;
        *program_size = geometry.program_size;
        *erase_size = geometry.erase_size;
        *erase_count = geometry.erase_count;
    }
    return platform::linux::littlefs_provider::to(status);
}

int mm_linux_lfs_provider_read(void* context, uint64_t offset, void* data, size_t size) {
    auto& device = *static_cast<mm::fs::FlashDevice*>(context);
    return platform::linux::littlefs_provider::to(
        device.read(offset, std::span<std::byte>{static_cast<std::byte*>(data), size}));
}

int mm_linux_lfs_provider_program(void* context, uint64_t offset, const void* data,
                                 size_t size) {
    auto& device = *static_cast<mm::fs::FlashDevice*>(context);
    return platform::linux::littlefs_provider::to(device.program(
        offset, std::span<const std::byte>{static_cast<const std::byte*>(data), size}));
}

int mm_linux_lfs_provider_erase(void* context, uint64_t offset, uint64_t size) {
    auto& device = *static_cast<mm::fs::FlashDevice*>(context);
    return platform::linux::littlefs_provider::to(device.erase(offset, size));
}

int mm_linux_lfs_provider_sync(void* context) {
    auto& device = *static_cast<mm::fs::FlashDevice*>(context);
    return platform::linux::littlefs_provider::to(device.sync());
}

uint64_t mm_linux_lfs_provider_now(void) { return mm::fs::now(); }

}  // extern "C"

namespace platform::linux::littlefs_provider {

void install_clock() { mm_linux_lfs_set_clock(&mm_linux_lfs_provider_now); }

mm_linux_lfs_device device_record(mm::fs::FlashDevice& device) {
    return {&device,
            &mm_linux_lfs_provider_geometry,
            &mm_linux_lfs_provider_read,
            &mm_linux_lfs_provider_program,
            &mm_linux_lfs_provider_erase,
            &mm_linux_lfs_provider_sync};
}

mm::fs::Status attach_volume(mm::fs::FlashDevice& device, bool read_only, bool format_if_blank,
                             std::int32_t block_cycles, mm::fs::Volume*& volume) {
    for (auto& candidate : volumes) {
        if (candidate.used) continue;
        candidate.device = device_record(device);
        unsigned int index = 0;
        const auto status = from(mm_linux_lfs_attach(&candidate.device, read_only ? 1 : 0,
                                                    format_if_blank ? 1 : 0, block_cycles,
                                                    &index));
        if (status != mm::fs::Status::Ok) return status;
        candidate.used = true;
        candidate.index = index;
        volume = &candidate;
        return mm::fs::Status::Ok;
    }
    return mm::fs::Status::TooMany;
}

mm::fs::Status detach_volume(mm::fs::Volume& volume) {
    for (auto& candidate : volumes) {
        if (&candidate != &volume || !candidate.used) continue;
        candidate.used = false;
        return from(mm_linux_lfs_detach(candidate.index));
    }
    return mm::fs::Status::BadArgument;
}

mm::fs::Status LittlefsVolume::open(std::string_view path, mm::fs::Access access,
                                    mm::fs::Disposition disposition, mm::fs::Handle& handle) {
    int mode = MM_LINUX_LFS_READ;
    switch (access) {
        case mm::fs::Access::Read: mode = MM_LINUX_LFS_READ; break;
        case mm::fs::Access::Write: mode = MM_LINUX_LFS_WRITE; break;
        case mm::fs::Access::ReadWrite: mode = MM_LINUX_LFS_READ_WRITE; break;
        case mm::fs::Access::Append: mode = MM_LINUX_LFS_APPEND; break;
    }
    int how = MM_LINUX_LFS_OPEN_EXISTING;
    switch (disposition) {
        case mm::fs::Disposition::OpenExisting: how = MM_LINUX_LFS_OPEN_EXISTING; break;
        case mm::fs::Disposition::OpenOrCreate: how = MM_LINUX_LFS_OPEN_OR_CREATE; break;
        case mm::fs::Disposition::CreateNew: how = MM_LINUX_LFS_CREATE_NEW; break;
        case mm::fs::Disposition::CreateOrTruncate: how = MM_LINUX_LFS_CREATE_OR_TRUNCATE; break;
    }
    unsigned int opened = 0;
    const auto status =
        from(mm_linux_lfs_open(index, path.data(), path.size(), mode, how, &opened));
    if (status == mm::fs::Status::Ok) handle = opened;
    return status;
}

mm::fs::Status LittlefsVolume::read(mm::fs::Handle handle, std::span<std::byte> into,
                                    std::size_t& count) {
    return from(mm_linux_lfs_read(handle, into.data(), into.size(), &count));
}

mm::fs::Status LittlefsVolume::write(mm::fs::Handle handle, std::span<const std::byte> from_,
                                     std::size_t& count) {
    return from(mm_linux_lfs_write(handle, from_.data(), from_.size(), &count));
}

mm::fs::Status LittlefsVolume::seek(mm::fs::Handle handle, std::uint64_t offset) {
    return from(mm_linux_lfs_seek(handle, offset));
}

mm::fs::Status LittlefsVolume::tell(mm::fs::Handle handle, std::uint64_t& offset) {
    return from(mm_linux_lfs_tell(handle, &offset));
}

mm::fs::Status LittlefsVolume::truncate(mm::fs::Handle handle) {
    return from(mm_linux_lfs_truncate(handle));
}

mm::fs::Status LittlefsVolume::sync(mm::fs::Handle handle) {
    return from(mm_linux_lfs_sync(handle));
}

mm::fs::Status LittlefsVolume::file_stat(mm::fs::Handle handle, mm::fs::Stat& stat) {
    mm_linux_lfs_entry entry{};
    const auto status = from(mm_linux_lfs_file_stat(handle, &entry));
    if (status == mm::fs::Status::Ok) stat = stat_of(entry);
    return status;
}

mm::fs::Status LittlefsVolume::close(mm::fs::Handle handle) {
    return from(mm_linux_lfs_close(handle));
}

mm::fs::Status LittlefsVolume::open_directory(std::string_view path, mm::fs::Handle& handle) {
    unsigned int opened = 0;
    const auto status = from(mm_linux_lfs_open_directory(index, path.data(), path.size(), &opened));
    if (status == mm::fs::Status::Ok) handle = opened;
    return status;
}

mm::fs::Status LittlefsVolume::next(mm::fs::Handle handle, std::span<char> name,
                                    std::size_t& length, mm::fs::Stat& stat, bool& done) {
    mm_linux_lfs_entry entry{};
    std::size_t taken = 0;
    int finished = 0;
    const auto status = from(
        mm_linux_lfs_next(handle, name.data(), name.size(), &taken, &entry, &finished));
    if (status != mm::fs::Status::Ok) return status;
    done = finished != 0;
    if (!done) {
        length = taken;
        stat = stat_of(entry);
    }
    return status;
}

mm::fs::Status LittlefsVolume::close_directory(mm::fs::Handle handle) {
    return from(mm_linux_lfs_close_directory(handle));
}

mm::fs::Status LittlefsVolume::stat(std::string_view path, mm::fs::Stat& stat) {
    mm_linux_lfs_entry entry{};
    const auto status = from(mm_linux_lfs_stat(index, path.data(), path.size(), &entry));
    if (status == mm::fs::Status::Ok) stat = stat_of(entry);
    return status;
}

mm::fs::Status LittlefsVolume::make_directory(std::string_view path) {
    return from(mm_linux_lfs_make_directory(index, path.data(), path.size()));
}

mm::fs::Status LittlefsVolume::remove(std::string_view path) {
    return from(mm_linux_lfs_remove(index, path.data(), path.size()));
}

mm::fs::Status LittlefsVolume::rename(std::string_view from_, std::string_view to_) {
    return from(mm_linux_lfs_rename(index, from_.data(), from_.size(), to_.data(), to_.size()));
}

mm::fs::Status LittlefsVolume::space(mm::fs::Space& space) {
    std::uint64_t total = 0;
    std::uint64_t free = 0;
    const auto status = from(mm_linux_lfs_space(index, &total, &free));
    if (status == mm::fs::Status::Ok) space = {total, free};
    return status;
}

mm::fs::Status LittlefsVolume::flush() { return from(mm_linux_lfs_flush(index)); }

}  // namespace platform::linux::littlefs_provider
