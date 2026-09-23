// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

module mm.shell.posix;

import :service;
import mm.shell.full;

namespace mm::shell::posix {
namespace {

// Which step of child setup failed, so the parent can tell a missing program
// from an unexecutable one from an infrastructure fault.
enum class Stage : int {
    Input = 1,
    Output = 2,
    Error = 3,
    Directory = 4,
    Exec = 5,
};

struct Report {
    int stage = 0;
    int error = 0;
};

// Terminated copies, built before the fork so the child allocates nothing.
struct Vectors {
    std::vector<std::string> storage;
    std::vector<char*> pointers;

    void build(std::span<const std::string_view> from) {
        storage.reserve(from.size());
        pointers.reserve(from.size() + 1);
        for (const auto entry : from) storage.emplace_back(entry);
        for (auto& entry : storage) pointers.push_back(entry.data());
        pointers.push_back(nullptr);
    }
};

[[noreturn]] void report_and_exit(int pipe_end, Stage stage, int error) {
    const Report report{static_cast<int>(stage), error};
    (void)::write(pipe_end, &report, sizeof(report));
    ::_exit(127);
}

void attach(int from, int to, int pipe_end, Stage stage) {
    if (from < 0) return;
    if (::dup2(from, to) >= 0) return;
    report_and_exit(pipe_end, stage, errno);
}

}  // namespace

full::ServiceStatus HostServices::spawn_callback(
    void* context, const full::ProcessRequest& request, full::Handle& out) {
    auto& self = *static_cast<HostServices*>(context);
    out = full::invalid_handle;
    if (request.arguments.empty()) return full::ServiceStatus::Invalid;

    Vectors argv;
    argv.build(request.arguments);
    Vectors envp;
    if (!request.environment.empty()) envp.build(request.environment);
    const std::string directory{request.directory};

    const auto input = self.descriptor_of(request.input);
    const auto output = self.descriptor_of(request.output);
    const auto error = self.descriptor_of(request.error);

    // A close-on-exec pipe carries a pre-exec failure. A successful exec
    // closes it with nothing written, which is how the parent sees success.
    int report_pipe[2]{-1, -1};
    if (::pipe(report_pipe) != 0) return full::ServiceStatus::Failed;
    (void)::fcntl(report_pipe[1], F_SETFD, FD_CLOEXEC);

    const auto child = ::fork();
    if (child < 0) {
        const auto failure = errno;
        (void)::close(report_pipe[0]);
        (void)::close(report_pipe[1]);
        return failure == EAGAIN ? full::ServiceStatus::WouldBlock
                                 : full::ServiceStatus::Failed;
    }
    if (child == 0) {
        (void)::close(report_pipe[0]);
        attach(static_cast<int>(input), 0, report_pipe[1],
               Stage::Input);
        attach(static_cast<int>(output), 1, report_pipe[1],
               Stage::Output);
        attach(static_cast<int>(error), 2, report_pipe[1],
               Stage::Error);
        if (!directory.empty() && ::chdir(directory.c_str()) != 0) {
            report_and_exit(report_pipe[1], Stage::Directory, errno);
        }
        if (envp.pointers.empty()) {
            (void)::execvp(argv.pointers[0], argv.pointers.data());
        } else {
            (void)::execvpe(argv.pointers[0], argv.pointers.data(),
                            envp.pointers.data());
        }
        report_and_exit(report_pipe[1], Stage::Exec, errno);
    }

    (void)::close(report_pipe[1]);
    Report report;
    std::size_t received = 0;
    while (received < sizeof(report)) {
        const auto moved = ::read(report_pipe[0],
                                  reinterpret_cast<char*>(&report) + received,
                                  sizeof(report) - received);
        if (moved <= 0) break;
        received += static_cast<std::size_t>(moved);
    }
    (void)::close(report_pipe[0]);

    if (received != sizeof(report)) {
        out = self.publish_child(child);
        return full::ServiceStatus::Ok;
    }
    // The child never ran the program, so it is reaped here and no handle is
    // published: the caller sees a spawn failure, not a child to wait for.
    int discarded = 0;
    (void)::waitpid(child, &discarded, 0);
    if (report.stage != static_cast<int>(Stage::Exec)) {
        return full::ServiceStatus::Failed;
    }
    if (report.error == ENOENT || report.error == ENOTDIR) {
        return full::ServiceStatus::NotFound;
    }
    if (report.error == EACCES || report.error == EPERM ||
        report.error == ENOEXEC || report.error == EISDIR) {
        return full::ServiceStatus::PermissionDenied;
    }
    return full::ServiceStatus::Failed;
}

full::ServiceStatus HostServices::wait_callback(void* context,
                                                full::Handle handle,
                                                int& status) {
    auto& self = *static_cast<HostServices*>(context);
    status = 0;
    const auto child = self.child_of(handle);
    if (child < 0) return full::ServiceStatus::Invalid;
    int raw = 0;
    while (true) {
        const auto reaped = ::waitpid(static_cast<int>(child), &raw, 0);
        if (reaped >= 0) break;
        if (errno == EINTR) continue;
        return full::ServiceStatus::Failed;
    }
    self.release_child(handle);
    if (WIFEXITED(raw)) {
        status = WEXITSTATUS(raw);
        return full::ServiceStatus::Ok;
    }
    if (WIFSIGNALED(raw)) {
        // The shell convention for a signalled child.
        status = 128 + WTERMSIG(raw);
        return full::ServiceStatus::Ok;
    }
    return full::ServiceStatus::Failed;
}

full::ServiceStatus HostServices::kill_callback(void* context,
                                                full::Handle handle,
                                                int number) {
    auto& self = *static_cast<HostServices*>(context);
    const auto child = self.child_of(handle);
    if (child < 0) return full::ServiceStatus::Invalid;
    if (::kill(static_cast<int>(child), number) != 0) {
        return errno == ESRCH ? full::ServiceStatus::NotFound
                              : full::ServiceStatus::Failed;
    }
    return full::ServiceStatus::Ok;
}

}  // namespace mm::shell::posix
