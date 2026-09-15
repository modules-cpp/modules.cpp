// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <poll.h>
#include <span>
#include <unistd.h>

export module platform.linux.stdio;

import mm.stdio;

namespace {

class LinuxConsole final : public mm::stdio::Console {
public:
    [[nodiscard]] mm::stdio::Status initialize() override {
        if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
            return mm::stdio::Status::TransportError;
        initialized_ = true;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status write(std::span<const std::byte> data,
                                           std::size_t& written) override {
        if (!initialized_) return mm::stdio::Status::NotInitialized;
        if (output_lost_) return mm::stdio::Status::TransportError;
        written = 0;
        while (written < data.size()) {
            const auto count = ::write(STDOUT_FILENO, data.data() + written,
                                       data.size() - written);
            if (count > 0) { written += static_cast<std::size_t>(count); continue; }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return mm::stdio::Status::Ok;
            if (count < 0 && (errno == EPIPE || errno == EIO || errno == EBADF))
                output_lost_ = true;
            return mm::stdio::Status::TransportError;
        }
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status read(std::span<std::byte> data,
                                          std::size_t& count) override {
        if (!initialized_) return mm::stdio::Status::NotInitialized;
        if (input_lost_) return mm::stdio::Status::TransportError;
        count = 0;
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        int ready;
        do ready = ::poll(&descriptor, 1, 0); while (ready < 0 && errno == EINTR);
        if (ready < 0) return mm::stdio::Status::TransportError;
        if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            input_lost_ = true;
            return mm::stdio::Status::TransportError;
        }
        if (ready == 0 || !(descriptor.revents & POLLIN)) return mm::stdio::Status::Ok;
        ssize_t got;
        do got = ::read(STDIN_FILENO, data.data(), data.size());
        while (got < 0 && errno == EINTR);
        if (got >= 0) { count = static_cast<std::size_t>(got); return mm::stdio::Status::Ok; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return mm::stdio::Status::Ok;
        if (errno == EPIPE || errno == EIO || errno == EBADF) input_lost_ = true;
        return mm::stdio::Status::TransportError;
    }

    [[nodiscard]] mm::stdio::Status flush() override {
        if (!initialized_) return mm::stdio::Status::NotInitialized;
        return output_lost_ ? mm::stdio::Status::TransportError : mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status connected(bool& value) override {
        if (!initialized_) return mm::stdio::Status::NotInitialized;
        value = !output_lost_;
        return mm::stdio::Status::Ok;
    }

private:
    bool initialized_ = false;
    bool output_lost_ = false;
    bool input_lost_ = false;
};

LinuxConsole console;
struct Register { Register() { mm::stdio::set_console(console); } };
const Register registered;

}
