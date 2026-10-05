// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "../../fat-cxx.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module platform.pico.fs.fat;

import mm.fs;
import mm.fs.fat;

namespace platform::pico::fat_provider {

// Results cross the C boundary as the adapter's integers, numbered as
// mm::fs::Status is; they are mapped by name both ways, never cast.
mm::fs::Status from(int code) {
    using mm::fs::Status;
    switch (code) {
        case MM_PICO_FAT_OK: return Status::Ok;
        case MM_PICO_FAT_BAD_ARGUMENT: return Status::BadArgument;
        case MM_PICO_FAT_UNSUPPORTED: return Status::Unsupported;
        case MM_PICO_FAT_NOT_FOUND: return Status::NotFound;
        case MM_PICO_FAT_EXISTS: return Status::Exists;
        case MM_PICO_FAT_NOT_DIRECTORY: return Status::NotDirectory;
        case MM_PICO_FAT_IS_DIRECTORY: return Status::IsDirectory;
        case MM_PICO_FAT_NOT_EMPTY: return Status::NotEmpty;
        case MM_PICO_FAT_NO_SPACE: return Status::NoSpace;
        case MM_PICO_FAT_READ_ONLY: return Status::ReadOnly;
        case MM_PICO_FAT_NAME_TOO_LONG: return Status::NameTooLong;
        case MM_PICO_FAT_TOO_MANY: return Status::TooMany;
        case MM_PICO_FAT_BUSY: return Status::Busy;
        case MM_PICO_FAT_CROSS_VOLUME: return Status::CrossVolume;
        case MM_PICO_FAT_CORRUPT: return Status::Corrupt;
        case MM_PICO_FAT_TIMEOUT: return Status::Timeout;
        default: return Status::TransportError;
    }
}

namespace {

[[nodiscard]] int to(mm::fs::Status status) {
    using mm::fs::Status;
    switch (status) {
        case Status::Ok: return MM_PICO_FAT_OK;
        case Status::BadArgument: return MM_PICO_FAT_BAD_ARGUMENT;
        case Status::Unsupported: return MM_PICO_FAT_UNSUPPORTED;
        case Status::NotFound: return MM_PICO_FAT_NOT_FOUND;
        case Status::Exists: return MM_PICO_FAT_EXISTS;
        case Status::NotDirectory: return MM_PICO_FAT_NOT_DIRECTORY;
        case Status::IsDirectory: return MM_PICO_FAT_IS_DIRECTORY;
        case Status::NotEmpty: return MM_PICO_FAT_NOT_EMPTY;
        case Status::NoSpace: return MM_PICO_FAT_NO_SPACE;
        case Status::ReadOnly: return MM_PICO_FAT_READ_ONLY;
        case Status::NameTooLong: return MM_PICO_FAT_NAME_TOO_LONG;
        case Status::TooMany: return MM_PICO_FAT_TOO_MANY;
        case Status::Busy: return MM_PICO_FAT_BUSY;
        case Status::CrossVolume: return MM_PICO_FAT_CROSS_VOLUME;
        case Status::Corrupt: return MM_PICO_FAT_CORRUPT;
        case Status::Timeout: return MM_PICO_FAT_TIMEOUT;
        case Status::TransportError: return MM_PICO_FAT_TRANSPORT_ERROR;
    }
    return MM_PICO_FAT_TRANSPORT_ERROR;
}

[[nodiscard]] mm::fs::Stat stat_of(const mm_pico_fat_entry& entry) {
    return {entry.directory != 0 ? mm::fs::Kind::Directory : mm::fs::Kind::File, entry.size,
            entry.modified, entry.read_only != 0};
}

}  // namespace

}  // namespace platform::pico::fat_provider

// The adapter calls these, so they have C language linkage; each casts its
// context back to the BlockDevice the record was made for.
extern "C" {

int mm_pico_fat_provider_geometry(void* context, uint64_t* count, unsigned int* size) {
    auto& device = *static_cast<mm::fs::BlockDevice*>(context);
    mm::fs::BlockGeometry geometry;
    const auto status = device.geometry(geometry);
    if (status == mm::fs::Status::Ok) {
        *count = geometry.count;
        *size = geometry.size;
    }
    return platform::pico::fat_provider::to(status);
}

int mm_pico_fat_provider_read(void* context, uint64_t block, void* data, size_t size) {
    auto& device = *static_cast<mm::fs::BlockDevice*>(context);
    return platform::pico::fat_provider::to(
        device.read(block, std::span<std::byte>{static_cast<std::byte*>(data), size}));
}

int mm_pico_fat_provider_write(void* context, uint64_t block, const void* data, size_t size) {
    auto& device = *static_cast<mm::fs::BlockDevice*>(context);
    return platform::pico::fat_provider::to(device.write(
        block, std::span<const std::byte>{static_cast<const std::byte*>(data), size}));
}

int mm_pico_fat_provider_sync(void* context) {
    auto& device = *static_cast<mm::fs::BlockDevice*>(context);
    return platform::pico::fat_provider::to(device.sync());
}

uint64_t mm_pico_fat_provider_now(void) { return mm::fs::now(); }

}  // extern "C"

namespace platform::pico::fat_provider {

void install_clock() { mm_pico_fat_set_clock(&mm_pico_fat_provider_now); }

mm_pico_fat_device device_record(mm::fs::BlockDevice& device) {
    return {&device, &mm_pico_fat_provider_geometry, &mm_pico_fat_provider_read,
            &mm_pico_fat_provider_write, &mm_pico_fat_provider_sync};
}

int format_code(mm::fs::fat::Format format) {
    switch (format) {
        case mm::fs::fat::Format::Automatic: return MM_PICO_FAT_FORMAT_AUTOMATIC;
        case mm::fs::fat::Format::Fat: return MM_PICO_FAT_FORMAT_FAT;
        case mm::fs::fat::Format::Fat32: return MM_PICO_FAT_FORMAT_FAT32;
    }
    return MM_PICO_FAT_FORMAT_AUTOMATIC;
}

mm::fs::Status attach_volume(mm::fs::BlockDevice& device, bool read_only,
                             mm::fs::Volume*& volume) {
    for (auto& candidate : volumes) {
        if (candidate.used) continue;
        candidate.device = device_record(device);
        unsigned int index = 0;
        const auto status =
            from(mm_pico_fat_attach(&candidate.device, read_only ? 1 : 0, &index));
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
        return from(mm_pico_fat_detach(candidate.index));
    }
    return mm::fs::Status::BadArgument;
}

mm::fs::Status FatVolume::open(std::string_view path, mm::fs::Access access,
                                    mm::fs::Disposition disposition, mm::fs::Handle& handle) {
    int mode = MM_PICO_FAT_READ;
    switch (access) {
        case mm::fs::Access::Read: mode = MM_PICO_FAT_READ; break;
        case mm::fs::Access::Write: mode = MM_PICO_FAT_WRITE; break;
        case mm::fs::Access::ReadWrite: mode = MM_PICO_FAT_READ_WRITE; break;
        case mm::fs::Access::Append: mode = MM_PICO_FAT_APPEND; break;
    }
    int how = MM_PICO_FAT_OPEN_EXISTING;
    switch (disposition) {
        case mm::fs::Disposition::OpenExisting: how = MM_PICO_FAT_OPEN_EXISTING; break;
        case mm::fs::Disposition::OpenOrCreate: how = MM_PICO_FAT_OPEN_OR_CREATE; break;
        case mm::fs::Disposition::CreateNew: how = MM_PICO_FAT_CREATE_NEW; break;
        case mm::fs::Disposition::CreateOrTruncate: how = MM_PICO_FAT_CREATE_OR_TRUNCATE; break;
    }
    unsigned int opened = 0;
    const auto status =
        from(mm_pico_fat_open(index, path.data(), path.size(), mode, how, &opened));
    if (status == mm::fs::Status::Ok) handle = opened;
    return status;
}

mm::fs::Status FatVolume::read(mm::fs::Handle handle, std::span<std::byte> into,
                                    std::size_t& count) {
    return from(mm_pico_fat_read(handle, into.data(), into.size(), &count));
}

mm::fs::Status FatVolume::write(mm::fs::Handle handle, std::span<const std::byte> from_,
                                     std::size_t& count) {
    return from(mm_pico_fat_write(handle, from_.data(), from_.size(), &count));
}

mm::fs::Status FatVolume::seek(mm::fs::Handle handle, std::uint64_t offset) {
    return from(mm_pico_fat_seek(handle, offset));
}

mm::fs::Status FatVolume::tell(mm::fs::Handle handle, std::uint64_t& offset) {
    return from(mm_pico_fat_tell(handle, &offset));
}

mm::fs::Status FatVolume::truncate(mm::fs::Handle handle) {
    return from(mm_pico_fat_truncate(handle));
}

mm::fs::Status FatVolume::sync(mm::fs::Handle handle) {
    return from(mm_pico_fat_sync(handle));
}

mm::fs::Status FatVolume::file_stat(mm::fs::Handle handle, mm::fs::Stat& stat) {
    mm_pico_fat_entry entry{};
    const auto status = from(mm_pico_fat_file_stat(handle, &entry));
    if (status == mm::fs::Status::Ok) stat = stat_of(entry);
    return status;
}

mm::fs::Status FatVolume::close(mm::fs::Handle handle) {
    return from(mm_pico_fat_close(handle));
}

mm::fs::Status FatVolume::open_directory(std::string_view path, mm::fs::Handle& handle) {
    unsigned int opened = 0;
    const auto status = from(mm_pico_fat_open_directory(index, path.data(), path.size(), &opened));
    if (status == mm::fs::Status::Ok) handle = opened;
    return status;
}

mm::fs::Status FatVolume::next(mm::fs::Handle handle, std::span<char> name,
                                    std::size_t& length, mm::fs::Stat& stat, bool& done) {
    mm_pico_fat_entry entry{};
    std::size_t taken = 0;
    int finished = 0;
    const auto status = from(
        mm_pico_fat_next(handle, name.data(), name.size(), &taken, &entry, &finished));
    if (status != mm::fs::Status::Ok) return status;
    done = finished != 0;
    if (!done) {
        length = taken;
        stat = stat_of(entry);
    }
    return status;
}

mm::fs::Status FatVolume::close_directory(mm::fs::Handle handle) {
    return from(mm_pico_fat_close_directory(handle));
}

mm::fs::Status FatVolume::stat(std::string_view path, mm::fs::Stat& stat) {
    mm_pico_fat_entry entry{};
    const auto status = from(mm_pico_fat_stat(index, path.data(), path.size(), &entry));
    if (status == mm::fs::Status::Ok) stat = stat_of(entry);
    return status;
}

mm::fs::Status FatVolume::make_directory(std::string_view path) {
    return from(mm_pico_fat_make_directory(index, path.data(), path.size()));
}

mm::fs::Status FatVolume::remove(std::string_view path) {
    return from(mm_pico_fat_remove(index, path.data(), path.size()));
}

mm::fs::Status FatVolume::rename(std::string_view from_, std::string_view to_) {
    return from(mm_pico_fat_rename(index, from_.data(), from_.size(), to_.data(), to_.size()));
}

mm::fs::Status FatVolume::space(mm::fs::Space& space) {
    std::uint64_t total = 0;
    std::uint64_t free = 0;
    const auto status = from(mm_pico_fat_space(index, &total, &free));
    if (status == mm::fs::Status::Ok) space = {total, free};
    return status;
}

mm::fs::Status FatVolume::flush() { return from(mm_pico_fat_flush(index)); }

}  // namespace platform::pico::fat_provider
