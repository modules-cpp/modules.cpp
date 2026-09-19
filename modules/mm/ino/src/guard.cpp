// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

module mm.ino;

namespace mm::ino {

bool write_guarded(const std::filesystem::path& app_dir,
                   std::string_view target_filename,
                   std::string_view content,
                   std::string& error,
                   std::string_view temp_filename) {
    error.clear();
    const std::string target_str(target_filename);
    const std::string temp_str = temp_filename.empty()
        ? (target_str + ".tmp")
        : std::string(temp_filename);

    const int dir_fd =
        ::open(app_dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0) {
        error = "cannot open directory " + app_dir.string() + ": " +
                std::strerror(errno);
        return false;
    }

    const int file_fd = ::openat(dir_fd, temp_str.c_str(),
                                 O_CREAT | O_EXCL | O_NOFOLLOW | O_WRONLY |
                                     O_CLOEXEC,
                                 0666);
    if (file_fd < 0) {
        const int err = errno;
        ::close(dir_fd);
        if (err == EEXIST) {
            error = "temporary file " + temp_str + " already exists";
        } else {
            error = "cannot create temporary file " + temp_str + ": " +
                    std::strerror(err);
        }
        return false;
    }

    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(file_fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            const int err = errno;
            ::close(file_fd);
            ::unlinkat(dir_fd, temp_str.c_str(), 0);
            ::close(dir_fd);
            error = "write error on " + temp_str + ": " + std::strerror(err);
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }

    if (::close(file_fd) < 0) {
        const int err = errno;
        ::unlinkat(dir_fd, temp_str.c_str(), 0);
        ::close(dir_fd);
        error = "close error on " + temp_str + ": " + std::strerror(err);
        return false;
    }

    if (::renameat(dir_fd, temp_str.c_str(), dir_fd, target_str.c_str()) < 0) {
        const int err = errno;
        ::unlinkat(dir_fd, temp_str.c_str(), 0);
        ::close(dir_fd);
        error = "cannot rename " + temp_str + " to " + target_str + ": " +
                std::strerror(err);
        return false;
    }

    ::close(dir_fd);
    return true;
}

}  // namespace mm::ino
