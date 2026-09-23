// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

module mm.shell.posix;

import :service;
import mm.shell.full;

namespace mm::shell::posix {
namespace {

// Written as comparisons rather than a switch because EAGAIN and EWOULDBLOCK
// are the same value on some hosts and distinct on others, and the project
// admits no preprocessor directive but #include.
[[nodiscard]] full::ServiceStatus classify(int error) {
    if (error == ENOENT || error == ENOTDIR) {
        return full::ServiceStatus::NotFound;
    }
    if (error == EACCES || error == EPERM || error == EROFS) {
        return full::ServiceStatus::PermissionDenied;
    }
    if (error == EINTR) return full::ServiceStatus::Interrupted;
    if (error == EAGAIN || error == EWOULDBLOCK) {
        return full::ServiceStatus::WouldBlock;
    }
    if (error == EBADF || error == EINVAL) {
        return full::ServiceStatus::Invalid;
    }
    return full::ServiceStatus::Failed;
}

}  // namespace

full::ServiceStatus HostServices::open_callback(void* context,
                                               std::string_view path,
                                               full::OpenMode mode,
                                               full::Handle& out) {
    auto& self = *static_cast<HostServices*>(context);
    out = full::invalid_handle;
    int flags = 0;
    switch (mode) {
        case full::OpenMode::Read: flags = O_RDONLY; break;
        case full::OpenMode::Truncate:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case full::OpenMode::Append:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            break;
    }
    // The service takes a view, and open needs a terminated string.
    const std::string name{path};
    const int descriptor = ::open(name.c_str(), flags, 0666);
    if (descriptor < 0) return classify(errno);
    out = self.publish_descriptor(descriptor, true);
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::pipe_callback(void* context,
                                                full::Handle& read_end,
                                                full::Handle& write_end) {
    auto& self = *static_cast<HostServices*>(context);
    read_end = full::invalid_handle;
    write_end = full::invalid_handle;
    int ends[2]{-1, -1};
    if (::pipe(ends) != 0) return classify(errno);
    read_end = self.publish_descriptor(ends[0], true);
    write_end = self.publish_descriptor(ends[1], true);
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::read_callback(void* context,
                                                full::Handle handle,
                                                std::span<std::byte> bytes,
                                                std::size_t& count) {
    auto& self = *static_cast<HostServices*>(context);
    count = 0;
    const auto descriptor = self.descriptor_of(handle);
    if (descriptor < 0) return full::ServiceStatus::Invalid;
    const auto moved = ::read(static_cast<int>(descriptor), bytes.data(),
                              bytes.size());
    if (moved < 0) return classify(errno);
    count = static_cast<std::size_t>(moved);
    return full::ServiceStatus::Ok;
}

// A partial count is reported rather than looped over here, because the shell
// boundary distinguishes Accepted from WouldBlock and the pump owns retrying.
full::ServiceStatus HostServices::write_callback(
    void* context, full::Handle handle, std::span<const std::byte> bytes,
    std::size_t& count) {
    auto& self = *static_cast<HostServices*>(context);
    count = 0;
    const auto descriptor = self.descriptor_of(handle);
    if (descriptor < 0) return full::ServiceStatus::Invalid;
    const auto moved = ::write(static_cast<int>(descriptor), bytes.data(),
                               bytes.size());
    if (moved < 0) return classify(errno);
    count = static_cast<std::size_t>(moved);
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::close_callback(void* context,
                                                full::Handle handle) {
    auto& self = *static_cast<HostServices*>(context);
    if (handle == 0 || handle > self.descriptors_.size()) {
        return full::ServiceStatus::Invalid;
    }
    const auto entry = self.descriptors_[handle - 1];
    if (entry.value < 0) return full::ServiceStatus::Invalid;
    self.release_descriptor(handle);
    // A borrowed descriptor belongs to whoever opened it; releasing the slot
    // is the whole effect.
    if (!entry.owned) return full::ServiceStatus::Ok;
    if (::close(static_cast<int>(entry.value)) != 0) return classify(errno);
    return full::ServiceStatus::Ok;
}

void HostServices::close_all() {
    for (auto& entry : descriptors_) {
        if (entry.value < 0) continue;
        if (entry.owned) (void)::close(static_cast<int>(entry.value));
        entry = {-1, true};
    }
}

}  // namespace mm::shell::posix
