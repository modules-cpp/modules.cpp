// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.fs.shell;

import mm.fs;
import mm.shell.full;

namespace mm::fs::shell {

namespace {

using mm::shell::full::ServiceStatus;

[[nodiscard]] ServiceStatus service_of(mm::fs::Status status) {
    switch (status) {
        case mm::fs::Status::Ok: return ServiceStatus::Ok;
        case mm::fs::Status::NotFound:
        case mm::fs::Status::NotDirectory: return ServiceStatus::NotFound;
        case mm::fs::Status::ReadOnly: return ServiceStatus::PermissionDenied;
        case mm::fs::Status::BadArgument:
        case mm::fs::Status::NameTooLong: return ServiceStatus::Invalid;
        default: return ServiceStatus::Failed;
    }
}

}  // namespace

FileServices::FileServices(mm::shell::full::IoService base) : base_(base) {}

bool FileServices::set_directory(std::string_view directory) {
    std::array<char, mm::fs::max_path + 1> buffer{};
    std::size_t length = 0;
    if (mm::fs::normalize(directory, buffer, length) != mm::fs::Status::Ok) return false;
    directory_.assign(buffer.data(), length);
    return true;
}

bool FileServices::resolve(std::string_view path, std::string& out) const {
    std::string joined;
    if (path.empty() || path == ".") {
        joined = directory_;
    } else if (path.front() == '/') {
        joined = std::string{path};
    } else {
        joined = directory_;
        if (joined.back() != '/') joined += '/';
        joined += path;
    }
    std::array<char, mm::fs::max_path + 1> buffer{};
    std::size_t length = 0;
    if (mm::fs::normalize(joined, buffer, length) != mm::fs::Status::Ok) return false;
    out.assign(buffer.data(), length);
    return true;
}

mm::fs::File* FileServices::owned(mm::shell::full::Handle handle) {
    if ((handle & own) == 0) return nullptr;
    const auto index = handle & ~own;
    if (index >= files_.size() || !files_[index].is_open()) return nullptr;
    return &files_[index];
}

std::size_t FileServices::open_files() const {
    std::size_t count = 0;
    for (const auto& file : files_)
        if (file.is_open()) ++count;
    return count;
}

void FileServices::close_all() {
    for (auto& file : files_)
        if (file.is_open()) static_cast<void>(file.close());
}

mm::shell::full::IoService FileServices::io() {
    return {.context = this,
            .open = &open_callback,
            .pipe = &pipe_callback,
            .read = &read_callback,
            .write = &write_callback,
            .close = &close_callback};
}

mm::shell::full::FileService FileServices::file() {
    return {.context = this,
            .list = &list_callback,
            .status = &status_callback,
            .canonical = &canonical_callback,
            .test = &test_callback};
}

mm::shell::full::Services FileServices::all(mm::shell::full::Services others) {
    others.io = io();
    others.file = file();
    return others;
}

ServiceStatus FileServices::open_callback(void* context, std::string_view path,
                                          mm::shell::full::OpenMode mode,
                                          mm::shell::full::Handle& handle) {
    auto& self = *static_cast<FileServices*>(context);
    std::string resolved;
    if (!self.resolve(path, resolved)) return ServiceStatus::Invalid;
    auto access = mm::fs::Access::Read;
    auto disposition = mm::fs::Disposition::OpenExisting;
    switch (mode) {
        case mm::shell::full::OpenMode::Read: break;
        case mm::shell::full::OpenMode::Truncate:
            access = mm::fs::Access::Write;
            disposition = mm::fs::Disposition::CreateOrTruncate;
            break;
        case mm::shell::full::OpenMode::Append:
            access = mm::fs::Access::Append;
            disposition = mm::fs::Disposition::OpenOrCreate;
            break;
    }
    for (std::size_t i = 0; i < self.files_.size(); ++i) {
        if (self.files_[i].is_open()) continue;
        const auto status = mm::fs::open(resolved, access, disposition, self.files_[i]);
        if (status != mm::fs::Status::Ok) return service_of(status);
        handle = own | i;
        return ServiceStatus::Ok;
    }
    return ServiceStatus::Failed;    // every slot is open
}

ServiceStatus FileServices::pipe_callback(void* context, mm::shell::full::Handle& read_end,
                                          mm::shell::full::Handle& write_end) {
    auto& self = *static_cast<FileServices*>(context);
    if (self.base_.pipe == nullptr) return ServiceStatus::Invalid;
    return self.base_.pipe(self.base_.context, read_end, write_end);
}

ServiceStatus FileServices::read_callback(void* context, mm::shell::full::Handle handle,
                                          std::span<std::byte> bytes, std::size_t& count) {
    auto& self = *static_cast<FileServices*>(context);
    if ((handle & own) != 0) {
        auto* file = self.owned(handle);
        if (file == nullptr) return ServiceStatus::Invalid;
        return service_of(file->read(bytes, count));
    }
    if (self.base_.read == nullptr) return ServiceStatus::Invalid;
    return self.base_.read(self.base_.context, handle, bytes, count);
}

ServiceStatus FileServices::write_callback(void* context, mm::shell::full::Handle handle,
                                           std::span<const std::byte> bytes,
                                           std::size_t& count) {
    auto& self = *static_cast<FileServices*>(context);
    if ((handle & own) != 0) {
        auto* file = self.owned(handle);
        if (file == nullptr) return ServiceStatus::Invalid;
        return service_of(file->write(bytes, count));
    }
    if (self.base_.write == nullptr) return ServiceStatus::Invalid;
    return self.base_.write(self.base_.context, handle, bytes, count);
}

ServiceStatus FileServices::close_callback(void* context, mm::shell::full::Handle handle) {
    auto& self = *static_cast<FileServices*>(context);
    if ((handle & own) != 0) {
        auto* file = self.owned(handle);
        if (file == nullptr) return ServiceStatus::Invalid;
        return service_of(file->close());
    }
    if (self.base_.close == nullptr) return ServiceStatus::Invalid;
    return self.base_.close(self.base_.context, handle);
}

ServiceStatus FileServices::list_callback(void* context, std::string_view path,
                                          std::vector<std::string>& names) {
    auto& self = *static_cast<FileServices*>(context);
    std::string resolved;
    if (!self.resolve(path, resolved)) return ServiceStatus::Invalid;
    mm::fs::Directory directory;
    auto status = mm::fs::open_directory(resolved, directory);
    if (status != mm::fs::Status::Ok) return service_of(status);
    std::array<char, mm::fs::max_name + 1> name{};
    while (true) {
        std::size_t length = 0;
        mm::fs::Stat stat;
        bool done = false;
        status = directory.next(name, length, stat, done);
        if (status != mm::fs::Status::Ok) return service_of(status);
        if (done) break;
        names.emplace_back(name.data(), length);
    }
    return ServiceStatus::Ok;
}

ServiceStatus FileServices::status_callback(void* context, std::string_view path, bool& exists,
                                            bool& directory) {
    auto& self = *static_cast<FileServices*>(context);
    exists = false;
    directory = false;
    std::string resolved;
    if (!self.resolve(path, resolved)) return ServiceStatus::Invalid;
    mm::fs::Stat stat;
    const auto status = mm::fs::stat(resolved, stat);
    // A missing name is an answer, not a service failure.
    if (status == mm::fs::Status::NotFound || status == mm::fs::Status::NotDirectory)
        return ServiceStatus::Ok;
    if (status != mm::fs::Status::Ok) return service_of(status);
    exists = true;
    directory = stat.kind == mm::fs::Kind::Directory;
    return ServiceStatus::Ok;
}

ServiceStatus FileServices::canonical_callback(void* context, std::string_view path,
                                               std::string& out) {
    auto& self = *static_cast<FileServices*>(context);
    std::string resolved;
    if (!self.resolve(path, resolved)) return ServiceStatus::Invalid;
    mm::fs::Stat stat;
    const auto status = mm::fs::stat(resolved, stat);
    if (status != mm::fs::Status::Ok) return service_of(status);
    out = resolved;
    return ServiceStatus::Ok;
}

ServiceStatus FileServices::test_callback(void* context, std::string_view path,
                                          mm::shell::full::FilePredicate predicate,
                                          bool& answer) {
    auto& self = *static_cast<FileServices*>(context);
    answer = false;
    std::string resolved;
    if (!self.resolve(path, resolved)) return ServiceStatus::Ok;
    mm::fs::Stat stat;
    const auto status = mm::fs::stat(resolved, stat);
    if (status == mm::fs::Status::NotFound || status == mm::fs::Status::NotDirectory)
        return ServiceStatus::Ok;
    if (status != mm::fs::Status::Ok) return service_of(status);
    const bool directory = stat.kind == mm::fs::Kind::Directory;
    switch (predicate) {
        case mm::shell::full::FilePredicate::Exists:
        case mm::shell::full::FilePredicate::Readable: answer = true; break;
        case mm::shell::full::FilePredicate::Regular: answer = !directory; break;
        case mm::shell::full::FilePredicate::Directory: answer = directory; break;
        case mm::shell::full::FilePredicate::Writable: answer = !stat.read_only; break;
        // mm.fs has no execute permission; a directory may be entered.
        case mm::shell::full::FilePredicate::Executable: answer = directory; break;
        case mm::shell::full::FilePredicate::Nonempty: answer = stat.size > 0; break;
        case mm::shell::full::FilePredicate::SymbolicLink: answer = false; break;
    }
    return ServiceStatus::Ok;
}

}  // namespace mm::fs::shell
