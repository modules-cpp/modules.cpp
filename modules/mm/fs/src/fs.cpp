// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module mm.fs;

namespace mm::fs {

namespace {

// A mount slot. A prefix is "/" or "/" and one component, so max_name + 1
// bytes hold any of them. open counts the Files and Directories on the
// volume, which is how unmount knows it is Busy; drivers never see this table.
struct Mount {
    std::array<char, max_name + 1> prefix{};
    std::size_t prefix_length = 0;
    Volume* volume = nullptr;
    unsigned int open = 0;

    [[nodiscard]] std::string_view name() const { return {prefix.data(), prefix_length}; }
};

std::array<Mount, max_mounts> mounts;

Clock installed = nullptr;

// Normalised paths are built here rather than on the stack: mm.fs promises
// one caller at a time, and a Pico's stack is small. rename needs two.
std::array<char, max_path> first_path;
std::array<char, max_path> second_path;

// Where a path lands: the mount slot, and the path relative to its volume.
struct Resolved {
    unsigned int slot = 0;
    std::string_view relative;
};

[[nodiscard]] Status resolve(std::string_view path, std::array<char, max_path>& buffer,
                             Resolved& resolved) {
    std::size_t length = 0;
    const auto normalized = normalize(path, buffer, length);
    if (normalized != Status::Ok) return normalized;
    const std::string_view full{buffer.data(), length};

    // Prefixes are one component deep, so at most one non-root mount can
    // match, and it beats the root mount when both do.
    bool found_root = false;
    unsigned int root = 0;
    for (unsigned int slot = 0; slot < max_mounts; ++slot) {
        const auto& mount = mounts[slot];
        if (mount.volume == nullptr) continue;
        const auto prefix = mount.name();
        if (prefix == "/") {
            found_root = true;
            root = slot;
            continue;
        }
        if (full == prefix) {
            resolved = {slot, {}};
            return Status::Ok;
        }
        if (full.size() > prefix.size() && full.substr(0, prefix.size()) == prefix &&
            full[prefix.size()] == '/') {
            resolved = {slot, full.substr(prefix.size() + 1)};
            return Status::Ok;
        }
    }
    if (!found_root) return Status::NotFound;
    resolved = {root, full.substr(1)};
    return Status::Ok;
}

// The normalised prefix, checked for shape: "/" or "/name".
[[nodiscard]] Status prefix_of(std::string_view prefix, std::array<char, max_path>& buffer,
                               std::string_view& normalized) {
    std::size_t length = 0;
    const auto status = normalize(prefix, buffer, length);
    if (status != Status::Ok) return status;
    normalized = {buffer.data(), length};
    if (normalized.find('/', 1) != std::string_view::npos) return Status::BadArgument;
    return Status::Ok;
}

}  // namespace

void set_clock(Clock clock) { installed = clock; }

std::uint64_t now() { return installed != nullptr ? installed() : 0; }

Status mount(std::string_view prefix, Volume& volume) {
    std::string_view name;
    const auto shaped = prefix_of(prefix, first_path, name);
    if (shaped != Status::Ok) return shaped;

    Mount* free_slot = nullptr;
    for (auto& mount : mounts) {
        if (mount.volume == nullptr) {
            if (free_slot == nullptr) free_slot = &mount;
            continue;
        }
        if (mount.name() == name) return Status::Exists;
        if (mount.volume == &volume) return Status::Busy;
    }
    if (free_slot == nullptr) return Status::TooMany;

    std::copy(name.begin(), name.end(), free_slot->prefix.begin());
    free_slot->prefix_length = name.size();
    free_slot->volume = &volume;
    free_slot->open = 0;
    return Status::Ok;
}

Status unmount(std::string_view prefix) {
    std::string_view name;
    const auto shaped = prefix_of(prefix, first_path, name);
    if (shaped != Status::Ok) return shaped;

    for (auto& mount : mounts) {
        if (mount.volume == nullptr || mount.name() != name) continue;
        if (mount.open != 0) return Status::Busy;
        const auto flushed = mount.volume->flush();
        if (flushed != Status::Ok) return flushed;
        mount = Mount{};
        return Status::Ok;
    }
    return Status::NotFound;
}

Status mounted(std::string_view prefix, Volume*& volume) {
    std::string_view name;
    const auto shaped = prefix_of(prefix, first_path, name);
    if (shaped != Status::Ok) return shaped;
    for (const auto& mount : mounts) {
        if (mount.volume == nullptr || mount.name() != name) continue;
        volume = mount.volume;
        return Status::Ok;
    }
    return Status::NotFound;
}

Status open(std::string_view path, Access access, Disposition disposition, File& file) {
    if (file.is_open()) return Status::BadArgument;
    Resolved resolved;
    const auto status = resolve(path, first_path, resolved);
    if (status != Status::Ok) return status;
    auto& mount = mounts[resolved.slot];
    Handle handle = 0;
    const auto opened = mount.volume->open(resolved.relative, access, disposition, handle);
    if (opened != Status::Ok) return opened;
    file.volume_ = mount.volume;
    file.handle_ = handle;
    file.slot_ = resolved.slot;
    file.access_ = access;
    ++mount.open;
    return Status::Ok;
}

Status open_directory(std::string_view path, Directory& directory) {
    if (directory.is_open()) return Status::BadArgument;
    Resolved resolved;
    const auto status = resolve(path, first_path, resolved);
    if (status != Status::Ok) return status;
    auto& mount = mounts[resolved.slot];
    Handle handle = 0;
    const auto opened = mount.volume->open_directory(resolved.relative, handle);
    if (opened != Status::Ok) return opened;
    directory.volume_ = mount.volume;
    directory.handle_ = handle;
    directory.slot_ = resolved.slot;
    ++mount.open;
    return Status::Ok;
}

Status stat(std::string_view path, Stat& stat) {
    Resolved resolved;
    const auto status = resolve(path, first_path, resolved);
    if (status != Status::Ok) return status;
    return mounts[resolved.slot].volume->stat(resolved.relative, stat);
}

// A volume's root always exists and cannot be removed or renamed: the mount
// point is mm.fs's, not the volume's.
Status make_directory(std::string_view path) {
    Resolved resolved;
    const auto status = resolve(path, first_path, resolved);
    if (status != Status::Ok) return status;
    if (resolved.relative.empty()) return Status::Exists;
    return mounts[resolved.slot].volume->make_directory(resolved.relative);
}

Status remove(std::string_view path) {
    Resolved resolved;
    const auto status = resolve(path, first_path, resolved);
    if (status != Status::Ok) return status;
    if (resolved.relative.empty()) return Status::BadArgument;
    return mounts[resolved.slot].volume->remove(resolved.relative);
}

Status rename(std::string_view from, std::string_view to) {
    Resolved source;
    auto status = resolve(from, first_path, source);
    if (status != Status::Ok) return status;
    Resolved target;
    status = resolve(to, second_path, target);
    if (status != Status::Ok) return status;
    if (source.slot != target.slot) return Status::CrossVolume;
    if (source.relative.empty() || target.relative.empty()) return Status::BadArgument;
    return mounts[source.slot].volume->rename(source.relative, target.relative);
}

Status space(std::string_view path, Space& space) {
    Resolved resolved;
    const auto status = resolve(path, first_path, resolved);
    if (status != Status::Ok) return status;
    return mounts[resolved.slot].volume->space(space);
}

File::File(File&& other) noexcept
    : volume_(other.volume_), handle_(other.handle_), slot_(other.slot_),
      access_(other.access_) {
    other.volume_ = nullptr;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        if (is_open()) static_cast<void>(close());
        volume_ = other.volume_;
        handle_ = other.handle_;
        slot_ = other.slot_;
        access_ = other.access_;
        other.volume_ = nullptr;
    }
    return *this;
}

File::~File() {
    if (is_open()) static_cast<void>(close());
}

bool File::is_open() const { return volume_ != nullptr; }

void File::release() {
    if (mounts[slot_].open > 0) --mounts[slot_].open;
    volume_ = nullptr;
}

Status File::read(std::span<std::byte> into, std::size_t& count) {
    if (!is_open() || access_ == Access::Write || access_ == Access::Append)
        return Status::BadArgument;
    std::size_t moved = 0;
    const auto status = volume_->read(handle_, into, moved);
    if (status == Status::Ok) count = moved;
    return status;
}

Status File::write(std::span<const std::byte> from, std::size_t& count) {
    if (!is_open() || access_ == Access::Read) return Status::BadArgument;
    std::size_t moved = 0;
    const auto status = volume_->write(handle_, from, moved);
    if (status == Status::Ok || status == Status::NoSpace) count = moved;
    return status;
}

// A read-only file cannot be extended, so seeking past its end is refused
// here, the same on every volume.
Status File::seek(std::uint64_t offset) {
    if (!is_open()) return Status::BadArgument;
    if (access_ == Access::Read) {
        Stat current;
        const auto status = volume_->file_stat(handle_, current);
        if (status != Status::Ok) return status;
        if (offset > current.size) return Status::BadArgument;
    }
    return volume_->seek(handle_, offset);
}

Status File::tell(std::uint64_t& offset) const {
    if (!is_open()) return Status::BadArgument;
    std::uint64_t at = 0;
    const auto status = volume_->tell(handle_, at);
    if (status == Status::Ok) offset = at;
    return status;
}

Status File::truncate() {
    if (!is_open() || access_ == Access::Read) return Status::BadArgument;
    return volume_->truncate(handle_);
}

Status File::sync() {
    if (!is_open()) return Status::BadArgument;
    return volume_->sync(handle_);
}

Status File::stat(Stat& stat) const {
    if (!is_open()) return Status::BadArgument;
    return volume_->file_stat(handle_, stat);
}

Status File::close() {
    if (!is_open()) return Status::BadArgument;
    const auto status = volume_->close(handle_);
    release();
    return status;
}

Directory::Directory(Directory&& other) noexcept
    : volume_(other.volume_), handle_(other.handle_), slot_(other.slot_) {
    other.volume_ = nullptr;
}

Directory& Directory::operator=(Directory&& other) noexcept {
    if (this != &other) {
        if (is_open()) static_cast<void>(close());
        volume_ = other.volume_;
        handle_ = other.handle_;
        slot_ = other.slot_;
        other.volume_ = nullptr;
    }
    return *this;
}

Directory::~Directory() {
    if (is_open()) static_cast<void>(close());
}

bool Directory::is_open() const { return volume_ != nullptr; }

void Directory::release() {
    if (mounts[slot_].open > 0) --mounts[slot_].open;
    volume_ = nullptr;
}

Status Directory::next(std::span<char> name, std::size_t& length, Stat& stat, bool& done) {
    if (!is_open()) return Status::BadArgument;
    return volume_->next(handle_, name, length, stat, done);
}

Status Directory::close() {
    if (!is_open()) return Status::BadArgument;
    const auto status = volume_->close_directory(handle_);
    release();
    return status;
}

}  // namespace mm::fs
