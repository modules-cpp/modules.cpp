// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

export module platform.linux.defaults;

import platform.linux.map;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::defaults_provider {

const platform::linux::Map defaults{};

// The most an override may be: far beyond any map, small enough that reading
// it is not a wait.
constexpr std::size_t override_limit = 64 * 1024;

}

export namespace platform::linux {

// Reads an override file. This is the provider's work rather than the map
// module's: platform.linux.map is a platform interface and is built for every
// target, including ones whose libc has no O_CLOEXEC because it has no
// process to inherit a descriptor.
//
// The override is read by the first board query, which must return: the path
// is opened without blocking, the descriptor -- not the name -- is checked to
// be a regular file, and at most override_limit bytes are read, so neither a
// FIFO put in the file's place nor a file that grows while it is read can hold
// the query.
[[nodiscard]] inline MapStatus read_override(const std::string& path,
                                             std::string& text,
                                             ParseError& error) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        error = {path, 0, 0, {}, "cannot open override"};
        return MapStatus::FileError;
    }
    struct stat about{};
    if (::fstat(fd, &about) != 0 || !S_ISREG(about.st_mode)) {
        ::close(fd);
        error = {path, 0, 0, {}, "override is not a regular file"};
        return MapStatus::FileError;
    }
    text.clear();
    while (text.size() <= defaults_provider::override_limit) {
        char chunk[4096];
        const auto got = ::read(fd, chunk, sizeof chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) {
            ::close(fd);
            error = {path, 0, 0, {}, "cannot read override"};
            return MapStatus::FileError;
        }
        if (got == 0) break;
        text.append(chunk, static_cast<std::size_t>(got));
    }
    ::close(fd);
    if (text.size() > defaults_provider::override_limit) {
        error = {path, 0, 0, {}, "override exceeds the size limit"};
        return MapStatus::FileError;
    }
    return MapStatus::Ok;
}

// Read one override file and apply it. The file half is above; the parsing
// and validation are platform.linux.map's, where they are portable.
[[nodiscard]] inline MapStatus apply_override(Map& map, const std::string& path,
                                              ParseError& error) {
    std::string text;
    const auto status = read_override(path, text, error);
    if (status != MapStatus::Ok) return status;
    return apply_override_text(map, text, path, error);
}

}

namespace platform::linux::defaults_provider {

struct Register {
    Register() {
        platform::linux::set_map(defaults);
        platform::linux::set_override_reader(&platform::linux::read_override);
    }
};

const Register registered;

}
