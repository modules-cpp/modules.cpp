// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <cstddef>
#include <dirent.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

module mm.shell.posix;

import :service;
import mm.shell.full;

namespace mm::shell::posix {
namespace {

[[nodiscard]] full::ServiceStatus classify(int error) {
    switch (error) {
        case ENOENT:
        case ENOTDIR: return full::ServiceStatus::NotFound;
        case EACCES:
        case EPERM: return full::ServiceStatus::PermissionDenied;
        case EINTR: return full::ServiceStatus::Interrupted;
        default: return full::ServiceStatus::Failed;
    }
}

}  // namespace

// Names only: pathname expansion needs no byte of content and opens no file.
full::ServiceStatus HostServices::list_callback(
    void*, std::string_view path, std::vector<std::string>& out) {
    const std::string directory{path.empty() ? std::string_view{"."} : path};
    DIR* stream = ::opendir(directory.c_str());
    if (stream == nullptr) return classify(errno);
    errno = 0;
    while (true) {
        const dirent* entry = ::readdir(stream);
        if (entry == nullptr) break;
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        out.emplace_back(name);
    }
    const auto failure = errno;
    (void)::closedir(stream);
    if (failure != 0) {
        out.clear();
        return classify(failure);
    }
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::status_callback(void*,
                                                 std::string_view path,
                                                 bool& exists,
                                                 bool& directory) {
    exists = false;
    directory = false;
    const std::string name{path.empty() ? std::string_view{"."} : path};
    struct stat info{};
    if (::stat(name.c_str(), &info) != 0) {
        // A missing name is an answer, not a service failure.
        if (errno == ENOENT || errno == ENOTDIR) {
            return full::ServiceStatus::Ok;
        }
        return classify(errno);
    }
    exists = true;
    directory = S_ISDIR(info.st_mode);
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::canonical_callback(
    void*, std::string_view path, std::string& out) {
    std::error_code error;
    const auto resolved = std::filesystem::canonical(
        std::filesystem::path{path}, error);
    if (error) return classify(error.value());
    out = resolved.string();
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::test_callback(
    void*, std::string_view path, full::FilePredicate predicate,
    bool& answer) {
    answer = false;
    const std::string name{path};
    int mode = -1;
    switch (predicate) {
        case full::FilePredicate::Exists: mode = F_OK; break;
        case full::FilePredicate::Readable: mode = R_OK; break;
        case full::FilePredicate::Writable: mode = W_OK; break;
        case full::FilePredicate::Executable: mode = X_OK; break;
        default: break;
    }
    if (mode >= 0) {
        answer = ::access(name.c_str(), mode) == 0;
        return full::ServiceStatus::Ok;
    }
    struct stat info{};
    const auto inspected = predicate ==
        full::FilePredicate::SymbolicLink
        ? ::lstat(name.c_str(), &info) : ::stat(name.c_str(), &info);
    if (inspected != 0) {
        return errno == ENOENT || errno == ENOTDIR ||
                       errno == EACCES
                   ? full::ServiceStatus::Ok : classify(errno);
    }
    switch (predicate) {
        case full::FilePredicate::Regular:
            answer = S_ISREG(info.st_mode);
            break;
        case full::FilePredicate::Directory:
            answer = S_ISDIR(info.st_mode);
            break;
        case full::FilePredicate::Nonempty:
            answer = info.st_size > 0;
            break;
        case full::FilePredicate::SymbolicLink:
            answer = S_ISLNK(info.st_mode);
            break;
        default: break;
    }
    return full::ServiceStatus::Ok;
}

}  // namespace mm::shell::posix
