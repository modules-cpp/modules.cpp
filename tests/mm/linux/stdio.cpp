// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <span>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

import mm.test;
import mm.stdio;
import platform.linux.stdio;

namespace {

using mm::test::expect;

void stdio_initialize_sets_sigpipe_to_ignore() {
    static constexpr std::array monitored_signals{
        SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2};

    std::array<struct sigaction, monitored_signals.size()> before{};
    for (std::size_t i = 0; i < monitored_signals.size(); ++i) {
        sigaction(monitored_signals[i], nullptr, &before[i]);
    }

    struct sigaction original_pipe{};
    sigaction(SIGPIPE, nullptr, &original_pipe);

    // Reset SIGPIPE to default to assert that initialize alters it.
    struct sigaction reset_action{};
    reset_action.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &reset_action, nullptr);

    // This binary links one console provider and no stand-in, so the seam is
    // the Linux one. Ok rules out the unserved fallback, which answers
    // Unsupported to everything; a second registrant would show up here rather
    // than as a puzzling SIGPIPE result below.
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == mm::stdio::Status::Ok,
           "Linux console initialization succeeds, so the seam is a real provider");

    struct sigaction current_pipe{};
    sigaction(SIGPIPE, nullptr, &current_pipe);
    expect(current_pipe.sa_handler == SIG_IGN,
           "initialize sets SIGPIPE to SIG_IGN");

    // Assert that no other signal disposition was altered
    for (std::size_t i = 0; i < monitored_signals.size(); ++i) {
        struct sigaction current{};
        sigaction(monitored_signals[i], nullptr, &current);
        expect(current.sa_handler == before[i].sa_handler &&
                   current.sa_flags == before[i].sa_flags,
               "initialize alters no other signal disposition");
    }

    // Restore original SIGPIPE disposition
    sigaction(SIGPIPE, &original_pipe, nullptr);
}

// Swaps a pipe end onto one of the standard descriptors for the duration
// of a check, and puts the original back afterwards, so the console can be
// driven to its failure paths without touching the test runner's own I/O.
class BorrowedDescriptor {
public:
    BorrowedDescriptor(int target, int replacement)
        : target_(target), saved_(::dup(target)) {
        ::dup2(replacement, target);
    }
    ~BorrowedDescriptor() {
        if (saved_ >= 0) { ::dup2(saved_, target_); ::close(saved_); }
    }
    BorrowedDescriptor(const BorrowedDescriptor&) = delete;
    BorrowedDescriptor& operator=(const BorrowedDescriptor&) = delete;
    BorrowedDescriptor(BorrowedDescriptor&&) = delete;
    BorrowedDescriptor& operator=(BorrowedDescriptor&&) = delete;

private:
    int target_;
    int saved_;
};

// Fills a descriptor without blocking, then puts its flags back so the
// console meets a blocking end.
void fill(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    expect(flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0,
           "the fill is nonblocking");
    std::array<std::byte, 4096> page{};
    while (::write(fd, page.data(), page.size()) > 0) {}
    expect(::fcntl(fd, F_SETFL, flags) == 0, "the console sees a blocking end");
}

void interrupting_alarm(int) {}

void write_returns_under_a_storm_of_signals() {
    // A signal interrupts poll, and the console must recompute what is left
    // rather than start its wait again: a hundred interruptions a second
    // must not stretch a one-second call.
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == mm::stdio::Status::Ok, "console initializes");
    int ends[2]{};
    expect(::pipe(ends) == 0, "a pipe opens");
    fill(ends[1]);
    struct sigaction storm{};
    storm.sa_handler = &interrupting_alarm;  // no SA_RESTART: poll sees EINTR
    struct sigaction before{};
    expect(::sigaction(SIGALRM, &storm, &before) == 0, "an interrupting handler is set");
    itimerval every_ten_ms{{0, 10'000}, {0, 10'000}};
    itimerval off{{0, 0}, {0, 0}};
    expect(::setitimer(ITIMER_REAL, &every_ten_ms, nullptr) == 0, "the storm starts");
    std::array<std::byte, 4096> page{};
    std::size_t written = 77;
    mm::stdio::Status status;
    const auto start = std::chrono::steady_clock::now();
    {
        BorrowedDescriptor stdout_is_full(STDOUT_FILENO, ends[1]);
        status = console.write(page, written);
    }
    const auto took = std::chrono::steady_clock::now() - start;
    ::setitimer(ITIMER_REAL, &off, nullptr);
    ::sigaction(SIGALRM, &before, nullptr);
    ::close(ends[0]);
    ::close(ends[1]);
    expect(status == mm::stdio::Status::Ok && written == 0,
           "the interrupted write is Ok with nothing written");
    expect(took >= std::chrono::milliseconds(900) && took < std::chrono::seconds(3),
           "the signals did not stretch the deadline");
}

void write_to_a_full_socket_returns() {
    // A socket cannot be reopened through /proc, so the console sends with
    // MSG_DONTWAIT instead; a peer that has stopped reading must not hold it.
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == mm::stdio::Status::Ok, "console initializes");
    int ends[2]{};
    expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0, "a socket pair opens");
    fill(ends[1]);
    std::array<std::byte, 4096> page{};
    std::size_t written = 77;
    mm::stdio::Status status;
    const auto start = std::chrono::steady_clock::now();
    {
        BorrowedDescriptor stdout_is_a_socket(STDOUT_FILENO, ends[1]);
        status = console.write(page, written);
    }
    const auto took = std::chrono::steady_clock::now() - start;
    const int after = ::fcntl(ends[1], F_GETFL);
    ::close(ends[0]);
    ::close(ends[1]);
    expect(status == mm::stdio::Status::Ok && written == 0,
           "a full socket with a live peer is Ok with nothing written");
    expect(took >= std::chrono::milliseconds(900) && took < std::chrono::seconds(3),
           "the socket write returned at its deadline rather than blocking");
    expect(after >= 0 && (after & O_NONBLOCK) == 0,
           "the socket's own flags are left as they were");
}

void write_failure_leaves_written_alone() {
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == mm::stdio::Status::Ok, "console initializes");
    int ends[2]{};
    expect(::pipe(ends) == 0, "a pipe opens");
    ::close(ends[0]);  // no reader: the first write answers EPIPE
    const std::array<std::byte, 4> text{std::byte{'m'}, std::byte{'m'},
                                        std::byte{'\n'}, std::byte{'\n'}};
    std::size_t written = 77;
    mm::stdio::Status status;
    {
        BorrowedDescriptor stdout_is_broken(STDOUT_FILENO, ends[1]);
        status = console.write(text, written);
    }
    ::close(ends[1]);
    expect(status == mm::stdio::Status::TransportError && written == 77,
           "a broken pipe is TransportError and written keeps its sentinel");
    expect(console.write(text, written) == mm::stdio::Status::TransportError &&
               written == 77,
           "lost output stays lost, and still leaves written alone");
}

void read_failure_leaves_count_alone() {
    // A fresh provider state is not reachable from the test, and the console
    // above has recorded its output as lost; input is a separate record, so
    // the read paths are still live.
    auto& console = mm::stdio::selected_console();
    int ends[2]{};
    expect(::pipe(ends) == 0, "a pipe opens");
    ::close(ends[1]);  // no writer: the read end reports hang-up
    std::array<std::byte, 8> buffer{};
    std::size_t count = 77;
    mm::stdio::Status status;
    {
        BorrowedDescriptor stdin_is_closed(STDIN_FILENO, ends[0]);
        status = console.read(buffer, count);
    }
    ::close(ends[0]);
    expect(status == mm::stdio::Status::TransportError && count == 77,
           "a hung-up input is TransportError and count keeps its sentinel");
}

void write_returns_when_the_reader_stops_reading() {
    // A live reader that never reads: the pipe fills, and a blocking write
    // would wait for it forever. The console must give up within its
    // deadline and answer Ok with what it managed, which is nothing. This
    // runs before the broken-pipe check, since that one leaves the console's
    // output recorded as lost for the rest of the process.
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == mm::stdio::Status::Ok, "console initializes");
    int ends[2]{};
    expect(::pipe(ends) == 0, "a pipe opens");
    fill(ends[1]);
    std::array<std::byte, 4096> page{};
    std::size_t written = 77;
    mm::stdio::Status status;
    const auto start = std::chrono::steady_clock::now();
    {
        BorrowedDescriptor stdout_is_full(STDOUT_FILENO, ends[1]);
        status = console.write(page, written);
    }
    const auto took = std::chrono::steady_clock::now() - start;
    const int after = ::fcntl(ends[1], F_GETFL);
    ::close(ends[0]);
    ::close(ends[1]);
    expect(status == mm::stdio::Status::Ok && written == 0,
           "a full pipe with a live reader is Ok with nothing written");
    expect(after >= 0 && (after & O_NONBLOCK) == 0,
           "the inherited description's flags are left as they were");
    expect(took >= std::chrono::milliseconds(900) && took < std::chrono::seconds(3),
           "the write returned at its deadline rather than blocking");
}

const mm::test::case_ cases[]{
    {"SIGPIPE is SIG_IGN and others unaltered",
     &stdio_initialize_sets_sigpipe_to_ignore},
    {"full pipe does not block", &write_returns_when_the_reader_stops_reading},
    {"signals do not stretch the deadline", &write_returns_under_a_storm_of_signals},
    {"full socket does not block", &write_to_a_full_socket_returns},
    {"write failure preserves written", &write_failure_leaves_written_alone},
    {"read failure preserves count", &read_failure_leaves_count_alone},
};
const mm::test::registrar reg{"platform.linux.stdio", cases};

}
