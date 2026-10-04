// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

module platform.linux.fs;

import mm.fs;
import mm.fs.local;
import mm.fs.native;
import platform.linux.map;

namespace platform::linux::fs_testing {

mm::fs::Status status_of(std::error_code error) {
    using mm::fs::Status;
    if (!error) return Status::Ok;
    if (error == std::errc::no_such_file_or_directory) return Status::NotFound;
    if (error == std::errc::file_exists) return Status::Exists;
    if (error == std::errc::not_a_directory) return Status::NotDirectory;
    if (error == std::errc::is_a_directory) return Status::IsDirectory;
    if (error == std::errc::directory_not_empty) return Status::NotEmpty;
    if (error == std::errc::no_space_on_device) return Status::NoSpace;
    if (error == std::errc::read_only_file_system || error == std::errc::permission_denied ||
        error == std::errc::operation_not_permitted)
        return Status::ReadOnly;
    if (error == std::errc::filename_too_long) return Status::NameTooLong;
    if (error == std::errc::too_many_files_open ||
        error == std::errc::too_many_files_open_in_system)
        return Status::TooMany;
    if (error == std::errc::device_or_resource_busy) return Status::Busy;
    if (error == std::errc::cross_device_link) return Status::CrossVolume;
    return Status::TransportError;
}

}  // namespace platform::linux::fs_testing

namespace platform::linux::fs_provider {

namespace {

using fs_testing::status_of;
using mm::fs::Access;
using mm::fs::Disposition;
using mm::fs::Kind;
using mm::fs::Status;

constexpr std::ios_base::openmode both = std::ios_base::in | std::ios_base::out;

// What a failed stream call left in errno, which the C library the stream
// sits on sets: the only error report std::filebuf has.
[[nodiscard]] Status last_error() {
    const auto status = status_of(std::error_code{errno, std::generic_category()});
    return status == Status::Ok ? Status::TransportError : status;
}

// Whether something exists at full. status() with an error code reports a
// missing file as file_type::not_found, and any other failure through ec.
[[nodiscard]] Status exists(const std::filesystem::path& full, std::filesystem::file_status& found) {
    std::error_code error;
    found = std::filesystem::status(full, error);
    if (found.type() == std::filesystem::file_type::not_found) return Status::NotFound;
    if (error) return status_of(error);
    return Status::Ok;
}

// The parent of full must be a directory for anything to be made in it.
[[nodiscard]] Status parent_is_directory(const std::filesystem::path& full) {
    std::filesystem::file_status parent;
    const auto status = exists(full.parent_path(), parent);
    if (status != Status::Ok) return status;
    return std::filesystem::is_directory(parent) ? Status::Ok : Status::NotDirectory;
}

[[nodiscard]] bool same_or_beneath(const std::filesystem::path& candidate,
                                   const std::filesystem::path& directory) {
    const auto& c = candidate.native();
    const auto& d = directory.native();
    return c == d || (c.size() > d.size() && c.compare(0, d.size(), d) == 0 && c[d.size()] == '/');
}

[[nodiscard]] Status attach_volume(const std::filesystem::path& root, bool read_only,
                                   bool contain_symlinks, mm::fs::Volume*& volume) {
    for (auto& candidate : volumes) {
        if (candidate.in_use()) continue;
        const auto status = candidate.attach(root, read_only, contain_symlinks);
        if (status == Status::Ok) volume = &candidate;
        return status;
    }
    return Status::TooMany;
}

[[nodiscard]] Status detach_volume(mm::fs::Volume& volume) {
    for (auto& candidate : volumes) {
        if (&candidate != &volume || !candidate.in_use()) continue;
        candidate.detach();
        return Status::Ok;
    }
    return Status::BadArgument;
}

}  // namespace

Status DirectoryVolume::attach(const std::filesystem::path& root, bool read_only,
                               bool contain_symlinks) {
    std::filesystem::file_status found;
    const auto status = exists(root, found);
    if (status != Status::Ok) return status;
    if (!std::filesystem::is_directory(found)) return Status::NotDirectory;
    std::error_code error;
    auto canonical = std::filesystem::canonical(root, error);
    if (error) return status_of(error);
    root_ = std::move(canonical);
    read_only_ = read_only;
    contain_symlinks_ = contain_symlinks;
    in_use_ = true;
    return Status::Ok;
}

void DirectoryVolume::detach() {
    for (auto& slot : files_) {
        if (!slot.used) continue;
        slot.buffer.close();
        slot.used = false;
    }
    for (auto& slot : directories_) {
        slot.used = false;
        slot.iterator = {};
    }
    in_use_ = false;
}

// root / relative, refused as absent when contain_symlinks is on and links
// resolve it outside the root. mm.fs has already removed every "..".
Status DirectoryVolume::locate(std::string_view relative, std::filesystem::path& full) const {
    full = relative.empty() ? root_ : root_ / std::filesystem::path{relative};
    if (!contain_symlinks_) return Status::Ok;
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(full, error);
    if (error) return status_of(error);
    const auto inside = resolved.lexically_relative(root_);
    if (inside.empty() || *inside.begin() == "..") return Status::NotFound;
    return Status::Ok;
}

Status DirectoryVolume::describe(const std::filesystem::path& full, mm::fs::Stat& stat) const {
    std::filesystem::file_status found;
    const auto status = exists(full, found);
    if (status != Status::Ok) return status;
    mm::fs::Stat result;
    result.kind = std::filesystem::is_directory(found) ? Kind::Directory : Kind::File;
    std::error_code error;
    if (result.kind == Kind::File) {
        const auto size = std::filesystem::file_size(full, error);
        if (error) return status_of(error);
        result.size = size;
    }
    const auto written = std::filesystem::last_write_time(full, error);
    if (!error) {
        const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
        result.modified = seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0;
    }
    result.read_only =
        read_only_ || (found.permissions() & std::filesystem::perms::owner_write) ==
                          std::filesystem::perms::none;
    stat = result;
    return Status::Ok;
}

bool DirectoryVolume::open_on(const std::filesystem::path& full, bool writers_only) const {
    return std::any_of(files_.begin(), files_.end(), [&](const FileSlot& slot) {
        return slot.used && slot.path == full && (!writers_only || slot.access != Access::Read);
    });
}

bool DirectoryVolume::open_beneath(const std::filesystem::path& full) const {
    return std::any_of(files_.begin(), files_.end(),
                       [&](const FileSlot& slot) {
                           return slot.used && same_or_beneath(slot.path, full);
                       }) ||
           std::any_of(directories_.begin(), directories_.end(), [&](const DirectorySlot& slot) {
               return slot.used && same_or_beneath(slot.path, full);
           });
}

DirectoryVolume::FileSlot* DirectoryVolume::file(mm::fs::Handle handle) {
    return handle < files_.size() && files_[handle].used ? &files_[handle] : nullptr;
}

// std::filebuf has no open mode that creates without truncating and no
// no-replace mode before C++23, so the decision is made from status() first
// and the mode chosen to match. Another process could create the file in
// between; mm.fs's one-caller promise is about this program.
Status DirectoryVolume::open(std::string_view path, Access access, Disposition disposition,
                             mm::fs::Handle& handle) {
    if (path.empty()) return Status::IsDirectory;
    const bool writable = access != Access::Read;
    const bool truncating = disposition == Disposition::CreateOrTruncate;
    if ((writable || truncating) && read_only_) return Status::ReadOnly;

    std::filesystem::path full;
    auto status = locate(path, full);
    if (status != Status::Ok) return status;
    status = parent_is_directory(full);
    if (status != Status::Ok) return status;
    std::filesystem::file_status found;
    status = exists(full, found);
    const bool present = status == Status::Ok;
    if (!present && status != Status::NotFound) return status;

    if (present) {
        if (std::filesystem::is_directory(found)) return Status::IsDirectory;
        if (disposition == Disposition::CreateNew) return Status::Exists;
        if (open_on(full, false) && (writable || truncating || open_on(full, true)))
            return Status::Busy;
    } else {
        if (disposition == Disposition::OpenExisting) return Status::NotFound;
        if (read_only_) return Status::ReadOnly;
    }

    const auto free_slot = std::find_if(files_.begin(), files_.end(),
                                        [](const FileSlot& slot) { return !slot.used; });
    if (free_slot == files_.end()) return Status::TooMany;
    auto& slot = *free_slot;

    // Making or emptying the file is a separate step whenever the mode the
    // file is then opened in would not do it.
    if (!present || truncating) {
        std::filebuf maker;
        if (maker.open(full, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary) ==
            nullptr)
            return last_error();
        maker.close();
    }
    std::ios_base::openmode mode = std::ios_base::binary;
    switch (access) {
        case Access::Read: mode |= std::ios_base::in; break;
        case Access::Write:
        case Access::ReadWrite: mode |= both; break;
        case Access::Append: mode |= std::ios_base::app; break;
    }
    if (slot.buffer.open(full, mode) == nullptr) return last_error();
    slot.used = true;
    slot.path = full;
    slot.access = access;
    slot.writing = false;
    handle = static_cast<mm::fs::Handle>(free_slot - files_.begin());
    return Status::Ok;
}

// A filebuf open for both reading and writing must be repositioned between a
// write and a read, as a C stream must; a seek to where it already is does it.
Status DirectoryVolume::read(mm::fs::Handle handle, std::span<std::byte> into,
                             std::size_t& count) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    if (slot->writing) {
        if (slot->buffer.pubseekoff(0, std::ios_base::cur, both) == std::streampos(-1))
            return last_error();
        slot->writing = false;
    }
    const auto moved = slot->buffer.sgetn(reinterpret_cast<char*>(into.data()),
                                          static_cast<std::streamsize>(into.size()));
    count = moved > 0 ? static_cast<std::size_t>(moved) : 0;
    return Status::Ok;
}

Status DirectoryVolume::write(mm::fs::Handle handle, std::span<const std::byte> from,
                              std::size_t& count) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    if (!slot->writing && slot->access == Access::ReadWrite) {
        if (slot->buffer.pubseekoff(0, std::ios_base::cur, both) == std::streampos(-1))
            return last_error();
    }
    slot->writing = true;
    errno = 0;
    const auto moved = slot->buffer.sputn(reinterpret_cast<const char*>(from.data()),
                                          static_cast<std::streamsize>(from.size()));
    count = moved > 0 ? static_cast<std::size_t>(moved) : 0;
    if (count == from.size()) return Status::Ok;
    const auto status = last_error();
    return status == Status::TransportError ? Status::NoSpace : status;
}

Status DirectoryVolume::seek(mm::fs::Handle handle, std::uint64_t offset) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    if (slot->buffer.pubseekpos(static_cast<std::streamoff>(offset), both) == std::streampos(-1))
        return last_error();
    slot->writing = false;
    return Status::Ok;
}

Status DirectoryVolume::tell(mm::fs::Handle handle, std::uint64_t& offset) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    const auto at = slot->buffer.pubseekoff(0, std::ios_base::cur, both);
    if (at == std::streampos(-1)) return last_error();
    offset = static_cast<std::uint64_t>(static_cast<std::streamoff>(at));
    return Status::Ok;
}

Status DirectoryVolume::truncate(mm::fs::Handle handle) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    std::uint64_t at = 0;
    auto status = tell(handle, at);
    if (status != Status::Ok) return status;
    if (slot->buffer.pubsync() != 0) return last_error();
    std::error_code error;
    std::filesystem::resize_file(slot->path, at, error);
    if (error) return status_of(error);
    return seek(handle, at);
}

Status DirectoryVolume::sync(mm::fs::Handle handle) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    return slot->buffer.pubsync() == 0 ? Status::Ok : last_error();
}

Status DirectoryVolume::file_stat(mm::fs::Handle handle, mm::fs::Stat& stat) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    if (slot->buffer.pubsync() != 0) return last_error();
    return describe(slot->path, stat);
}

Status DirectoryVolume::close(mm::fs::Handle handle) {
    auto* slot = file(handle);
    if (slot == nullptr) return Status::BadArgument;
    const bool closed = slot->buffer.close() != nullptr;
    slot->used = false;
    slot->path.clear();
    return closed ? Status::Ok : last_error();
}

Status DirectoryVolume::open_directory(std::string_view path, mm::fs::Handle& handle) {
    std::filesystem::path full;
    auto status = locate(path, full);
    if (status != Status::Ok) return status;
    std::filesystem::file_status found;
    status = exists(full, found);
    if (status != Status::Ok) return status;
    if (!std::filesystem::is_directory(found)) return Status::NotDirectory;
    const auto free_slot = std::find_if(directories_.begin(), directories_.end(),
                                        [](const DirectorySlot& slot) { return !slot.used; });
    if (free_slot == directories_.end()) return Status::TooMany;
    std::error_code error;
    std::filesystem::directory_iterator iterator{full, error};
    if (error) return status_of(error);
    *free_slot = DirectorySlot{true, full, std::move(iterator)};
    handle = static_cast<mm::fs::Handle>(free_slot - directories_.begin());
    return Status::Ok;
}

// directory_iterator never yields "." or "..". An entry whose name does not
// fit is left where it is, so the caller can retry with a larger buffer.
Status DirectoryVolume::next(mm::fs::Handle handle, std::span<char> name, std::size_t& length,
                             mm::fs::Stat& stat, bool& done) {
    if (handle >= directories_.size() || !directories_[handle].used) return Status::BadArgument;
    auto& slot = directories_[handle];
    while (slot.iterator != std::filesystem::directory_iterator{}) {
        const auto entry = slot.iterator->path();
        mm::fs::Stat described;
        const auto status = describe(entry, described);
        if (status == Status::NotFound) {    // removed, or a dangling link
            std::error_code error;
            slot.iterator.increment(error);
            if (error) return status_of(error);
            continue;
        }
        if (status != Status::Ok) return status;
        const auto leaf = entry.filename().native();
        if (leaf.size() > name.size()) return Status::NameTooLong;
        std::copy(leaf.begin(), leaf.end(), name.begin());
        length = leaf.size();
        stat = described;
        done = false;
        std::error_code error;
        slot.iterator.increment(error);
        if (error) slot.iterator = {};
        return Status::Ok;
    }
    done = true;
    return Status::Ok;
}

Status DirectoryVolume::close_directory(mm::fs::Handle handle) {
    if (handle >= directories_.size() || !directories_[handle].used) return Status::BadArgument;
    directories_[handle] = DirectorySlot{};
    return Status::Ok;
}

Status DirectoryVolume::stat(std::string_view path, mm::fs::Stat& stat) {
    std::filesystem::path full;
    const auto status = locate(path, full);
    if (status != Status::Ok) return status;
    return describe(full, stat);
}

Status DirectoryVolume::make_directory(std::string_view path) {
    if (read_only_) return Status::ReadOnly;
    std::filesystem::path full;
    auto status = locate(path, full);
    if (status != Status::Ok) return status;
    status = parent_is_directory(full);
    if (status != Status::Ok) return status;
    std::error_code error;
    const bool made = std::filesystem::create_directory(full, error);
    if (error) return status_of(error);
    return made ? Status::Ok : Status::Exists;
}

Status DirectoryVolume::remove(std::string_view path) {
    if (read_only_) return Status::ReadOnly;
    std::filesystem::path full;
    auto status = locate(path, full);
    if (status != Status::Ok) return status;
    std::filesystem::file_status found;
    status = exists(full, found);
    if (status != Status::Ok) return status;
    if (open_beneath(full)) return Status::Busy;
    std::error_code error;
    const bool removed = std::filesystem::remove(full, error);
    if (error) return status_of(error);
    return removed ? Status::Ok : Status::NotFound;
}

// std::filesystem::rename replaces an existing target; mm.fs's contract does
// not, so the target is checked first.
Status DirectoryVolume::rename(std::string_view from, std::string_view to) {
    if (read_only_) return Status::ReadOnly;
    std::filesystem::path source;
    auto status = locate(from, source);
    if (status != Status::Ok) return status;
    std::filesystem::path target;
    status = locate(to, target);
    if (status != Status::Ok) return status;
    std::filesystem::file_status found;
    status = exists(source, found);
    if (status != Status::Ok) return status;
    std::error_code error;
    if (std::filesystem::symlink_status(target, error).type() !=
        std::filesystem::file_type::not_found) {
        if (error) return status_of(error);
        return Status::Exists;
    }
    status = parent_is_directory(target);
    if (status != Status::Ok) return status;
    if (same_or_beneath(target, source)) return Status::BadArgument;    // into itself
    if (open_beneath(source)) return Status::Busy;
    std::filesystem::rename(source, target, error);
    return status_of(error);
}

Status DirectoryVolume::space(mm::fs::Space& space) {
    std::error_code error;
    const auto info = std::filesystem::space(root_, error);
    if (error) return status_of(error);
    space = {info.capacity, info.available};
    return Status::Ok;
}

Status DirectoryVolume::flush() {
    Status result = Status::Ok;
    for (auto& slot : files_)
        if (slot.used && slot.buffer.pubsync() != 0) result = last_error();
    return result;
}

// The device map's directory.0, or the working directory when the map names
// none. A map that failed to resolve is the map's fault, reported as
// BadArgument as every other Linux provider reports it.
Status LocalProvider::attach(const mm::fs::local::Options& options, mm::fs::Volume*& volume) {
    const auto& resolution = platform::linux::resolve();
    std::filesystem::path root;
    bool writable = true;
    if (resolution.status == platform::linux::MapStatus::Ok && resolution.map != nullptr &&
        !resolution.map->directories.empty()) {
        root = resolution.map->directories.front().path;
        writable = resolution.map->directories.front().writable;
    } else if (resolution.status == platform::linux::MapStatus::Ok ||
               resolution.status == platform::linux::MapStatus::NoDefaults) {
        std::error_code error;
        root = std::filesystem::current_path(error);
        if (error) return status_of(error);
    } else {
        return Status::BadArgument;
    }
    return attach_volume(root, options.read_only || !writable, true, volume);
}

Status LocalProvider::detach(mm::fs::Volume& volume) { return detach_volume(volume); }

Status NativeProvider::attach(std::string_view root, const mm::fs::native::Options& options,
                              mm::fs::Volume*& volume) {
    const std::filesystem::path directory{root};
    if (!directory.is_absolute()) return Status::BadArgument;
    return attach_volume(directory, options.read_only, options.contain_symlinks, volume);
}

Status NativeProvider::detach(mm::fs::Volume& volume) { return detach_volume(volume); }

}  // namespace platform::linux::fs_provider
