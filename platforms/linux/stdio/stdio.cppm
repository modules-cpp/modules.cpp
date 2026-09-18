// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

export module platform.linux.stdio;

import mm.stdio;

namespace {

// How long one write may spend before it returns with what it managed.
constexpr unsigned long output_deadline_ms = 1000;

// Every write goes through an operation that cannot wait on the far end.
// The inherited stdout shares its open file description -- and so its
// O_NONBLOCK flag -- with the shell that started the program, so the
// console must not set the flag there. What it does instead depends on
// what descriptor one is:
//
//   a pipe, a FIFO, or a terminal: reopened through /proc as a private
//   descriptor of the console's own, on which O_NONBLOCK is harmless, and a
//   write there returns what fitted;
//   a socket: cannot be reopened, but send with MSG_DONTWAIT is nonblocking
//   per call and needs no flag;
//   a regular file: a plain write, which waits on the disk and not on
//   anyone;
//   anything else, or a reopen that fails for a reason other than a missing
//   reader: no operation the console can bound, and the write is refused.
//
// The same object is checked on every write, so a descriptor redirected
// after initialize, as the tests do, is followed rather than bypassed.
enum class Path { Refused, Private, Socket, File, NoReader };

class Output {
public:
    Output() = default;
    ~Output() { release(); }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    Path resolve(int& fd) {
        struct stat now{};
        if (::fstat(STDOUT_FILENO, &now) != 0) { release(); return Path::Refused; }
        if (private_ >= 0 && now.st_dev == dev_ && now.st_ino == ino_ &&
            now.st_rdev == rdev_) {
            fd = private_;
            return Path::Private;
        }
        release();
        fd = STDOUT_FILENO;
        if (S_ISREG(now.st_mode)) return Path::File;
        if (S_ISSOCK(now.st_mode)) return Path::Socket;
        if (!S_ISFIFO(now.st_mode) && !S_ISCHR(now.st_mode)) return Path::Refused;
        const int reopened = ::open("/proc/self/fd/1",
                                    O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
        if (reopened < 0) return errno == ENXIO ? Path::NoReader : Path::Refused;
        private_ = reopened;
        dev_ = now.st_dev;
        ino_ = now.st_ino;
        rdev_ = now.st_rdev;
        fd = private_;
        return Path::Private;
    }

private:
    void release() {
        if (private_ >= 0) ::close(private_);
        private_ = -1;
    }
    int private_ = -1;
    dev_t dev_ = 0;
    ino_t ino_ = 0;
    dev_t rdev_ = 0;
};

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
        // A reader that has stopped reading, or a terminal that has stopped
        // draining, must not hold the program: every operation below returns
        // what fitted rather than waiting for room, and the call gives up at
        // output_deadline_ms with what it managed. The contract lets written
        // be short, and the caller bounds its retries. written changes only
        // on Ok, so progress is kept aside until then.
        int fd = STDOUT_FILENO;
        const auto path = output_.resolve(fd);
        if (path == Path::NoReader) {
            output_lost_ = true;
            return mm::stdio::Status::TransportError;
        }
        if (path == Path::Refused) return mm::stdio::Status::TransportError;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(output_deadline_ms);
        std::size_t progress = 0;
        while (progress < data.size()) {
            // Time left is recomputed on every pass, so a signal that
            // interrupts the poll costs the time it took and no more.
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) break;
            if (path != Path::File) {
                pollfd descriptor{fd, POLLOUT, 0};
                const int ready = ::poll(&descriptor, 1, static_cast<int>(left.count()));
                if (ready < 0 && errno == EINTR) continue;
                if (ready < 0) return mm::stdio::Status::TransportError;
                if (ready == 0) break;
                if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    output_lost_ = true;
                    return mm::stdio::Status::TransportError;
                }
            }
            const auto remaining = data.size() - progress;
            const auto count = path == Path::Socket
                ? ::send(fd, data.data() + progress, remaining, MSG_DONTWAIT | MSG_NOSIGNAL)
                : ::write(fd, data.data() + progress, remaining);
            if (count > 0) { progress += static_cast<std::size_t>(count); continue; }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (count < 0 && (errno == EPIPE || errno == EIO || errno == EBADF ||
                              errno == ECONNRESET || errno == ENOTCONN))
                output_lost_ = true;
            return mm::stdio::Status::TransportError;
        }
        written = progress;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status read(std::span<std::byte> data,
                                          std::size_t& count) override {
        if (!initialized_) return mm::stdio::Status::NotInitialized;
        if (input_lost_) return mm::stdio::Status::TransportError;
        // count changes only on Ok: every failure below leaves it untouched.
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        int ready;
        do ready = ::poll(&descriptor, 1, 0); while (ready < 0 && errno == EINTR);
        if (ready < 0) return mm::stdio::Status::TransportError;
        if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            input_lost_ = true;
            return mm::stdio::Status::TransportError;
        }
        if (ready == 0 || !(descriptor.revents & POLLIN)) {
            count = 0;
            return mm::stdio::Status::Ok;
        }
        ssize_t got;
        do got = ::read(STDIN_FILENO, data.data(), data.size());
        while (got < 0 && errno == EINTR);
        if (got >= 0) { count = static_cast<std::size_t>(got); return mm::stdio::Status::Ok; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            count = 0;
            return mm::stdio::Status::Ok;
        }
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
    Output output_;
};

LinuxConsole console;
struct Register { Register() { mm::stdio::set_console(console); } };
const Register registered;

}
