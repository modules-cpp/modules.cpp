// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

module mm.shell.posix;

import :service;
import mm.shell.full;
import mm.shell;

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

[[nodiscard]] bool beneath(const std::filesystem::path& path,
                           const std::filesystem::path& root) {
    auto place = path.begin();
    for (auto part = root.begin(); part != root.end(); ++part) {
        if (place == path.end() || *place != *part) return false;
        ++place;
    }
    return place != path.end();
}

[[nodiscard]] bool root_wrapper(std::string_view name) {
    for (const auto wrapper : {
             "bootstrap", "build", "check", "clean", "configure",
             "debug", "document", "flash", "json", "model",
             "run", "sketch", "test"}) {
        if (name == wrapper) return true;
    }
    return false;
}

enum class NativeKind { External, Native, Denied };

struct NativeCandidate {
    NativeKind kind = NativeKind::External;
    bool found = false;
    std::string source;
};

[[nodiscard]] NativeCandidate inspect_candidate(
    const std::filesystem::path& file,
    const std::filesystem::path& root) {
    std::error_code error;
    const auto lexical = std::filesystem::absolute(file, error)
                             .lexically_normal();
    if (error) return {};
    if (!beneath(lexical, root)) {
        return {.found = ::access(lexical.c_str(), X_OK) == 0};
    }
    const auto canonical = std::filesystem::canonical(lexical, error);
    if (error) return {};
    if (!beneath(canonical, root)) {
        return {.kind = NativeKind::Denied};
    }
    if (!std::filesystem::is_regular_file(canonical)) return {};
    const auto filename = canonical.filename().string();
    const bool script = canonical.extension() == ".sh" ||
        (canonical.parent_path() == root && root_wrapper(filename));
    if (!script) return {.found = true};
    if (::access(canonical.c_str(), R_OK | X_OK) != 0) {
        return {.kind = NativeKind::Denied};
    }
    std::ifstream input{canonical, std::ios::binary};
    if (!input) return {.kind = NativeKind::Denied};
    std::string source{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
    if (!source.starts_with("#!/bin/sh\n") &&
        source != "#!/bin/sh") return {.found = true};
    return {.kind = NativeKind::Native,
            .found = true,
            .source = std::move(source)};
}

[[nodiscard]] NativeCandidate native_candidate(
    const full::ProcessRequest& request,
    std::string_view project_root) {
    if (project_root.empty() || request.arguments.empty()) return {};
    const std::filesystem::path root{project_root};
    const auto command = request.arguments.front();
    const std::filesystem::path directory{request.directory};
    if (command.find('/') != std::string_view::npos) {
        const std::filesystem::path path{command};
        return inspect_candidate(path.is_absolute() ? path
                                 : directory / path, root);
    }
    std::string_view path_value;
    bool supplied = false;
    for (const auto entry : request.environment) {
        if (entry.starts_with("PATH=")) {
            path_value = entry.substr(5);
            supplied = true;
            break;
        }
    }
    if (!supplied) {
        if (const auto* inherited = std::getenv("PATH")) {
            path_value = inherited;
            supplied = true;
        }
    }
    if (!supplied) path_value = "/bin:/usr/bin";
    while (true) {
        const auto separator = path_value.find(':');
        const auto component = path_value.substr(0, separator);
        const std::filesystem::path part = component.empty()
            ? std::filesystem::path{"."}
            : std::filesystem::path{component};
        const std::filesystem::path base = part.is_absolute()
            ? part : directory / part;
        const auto candidate = inspect_candidate(base / command, root);
        if (candidate.found ||
            candidate.kind != NativeKind::External) return candidate;
        if (separator == std::string_view::npos) break;
        path_value.remove_prefix(separator + 1);
    }
    return {};
}

[[nodiscard]] std::vector<std::string> executable_paths(
    const full::ProcessRequest& request) {
    std::vector<std::string> paths;
    const auto command = request.arguments.front();
    if (command.find('/') != std::string_view::npos) {
        paths.emplace_back(command);
        return paths;
    }
    std::string_view value;
    bool supplied = false;
    for (const auto entry : request.environment) {
        if (!entry.starts_with("PATH=")) continue;
        value = entry.substr(5);
        supplied = true;
        break;
    }
    if (!supplied) {
        if (const auto* inherited = std::getenv("PATH")) {
            value = inherited;
            supplied = true;
        }
    }
    if (!supplied) value = "/bin:/usr/bin";
    while (true) {
        const auto separator = value.find(':');
        const auto component = value.substr(0, separator);
        std::string path = component.empty() ? "." :
            std::string(component);
        path.push_back('/');
        path.append(command);
        paths.push_back(std::move(path));
        if (separator == std::string_view::npos) break;
        value.remove_prefix(separator + 1);
    }
    return paths;
}

}  // namespace

full::ServiceStatus HostServices::spawn_callback(
    void* context, const full::ProcessRequest& request, full::Handle& out) {
    auto& self = *static_cast<HostServices*>(context);
    out = full::invalid_handle;
    if (request.arguments.empty()) return full::ServiceStatus::Invalid;
    const bool pipeline_shell = !request.native_source.empty();
    const auto native = pipeline_shell
        ? NativeCandidate{NativeKind::Native, true,
                          std::string(request.native_source)}
        : native_candidate(request, self.native_root_);
    if (native.kind == NativeKind::Denied ||
        (native.kind == NativeKind::Native &&
         self.native_depth_ >= 32)) {
        return full::ServiceStatus::PermissionDenied;
    }

    Vectors argv;
    argv.build(request.arguments);
    const auto paths = executable_paths(request);
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
        if (native.kind == NativeKind::Native) {
            // The parent must be free to launch the next pipeline stage.
            // A native child is a running process, not a pre-exec failure.
            (void)::close(report_pipe[1]);
            self.close_all();
            ++self.native_depth_;
            full::StateCapacity capacity = request.native_state == nullptr
                ? full::StateCapacity{}
                : request.native_state->capacity();
            std::size_t bytes = 0;
            for (const auto entry : request.environment) {
                bytes += entry.size() + 1;
            }
            if (capacity.variable_slots <
                request.environment.size() + 64) {
                capacity.variable_slots =
                    request.environment.size() + 64;
            }
            if (capacity.variable_bytes < bytes + 8192) {
                capacity.variable_bytes = bytes + 8192;
            }
            full::FullState state{capacity};
            if (request.native_state != nullptr) {
                if (request.native_state->fork_into(state) !=
                    mm::shell::Status::Ok) ::_exit(1);
            } else {
                if (state.seed_environment(request.environment) !=
                    mm::shell::Status::Ok) ::_exit(1);
                state.set_directory(directory);
            }
            full::Interpreter interpreter{state, self.all()};
            interpreter.set_streams({
                self.borrow_descriptor(0), self.borrow_descriptor(1),
                self.borrow_descriptor(2)});
            if (request.native_functions != nullptr) {
                (void)interpreter.functions().adopt_from(
                    *request.native_functions);
            }
            if (!pipeline_shell) {
                const auto arguments = request.arguments.subspan(1);
                if (interpreter.set_arguments(request.arguments.front(),
                                              arguments) !=
                    mm::shell::Status::Ok) ::_exit(2);
            }
            const auto outcome = interpreter.run_text(native.source);
            if (!outcome.ok()) ::_exit(2);
            ::_exit(outcome.status);
        }
        auto* environment = envp.pointers.empty()
            ? ::environ : envp.pointers.data();
        int executable_error = ENOENT;
        for (const auto& path : paths) {
            (void)::execve(path.c_str(), argv.pointers.data(),
                           environment);
            if (errno == EACCES) {
                executable_error = EACCES;
            } else if (errno != ENOENT && errno != ENOTDIR) {
                executable_error = errno;
                break;
            }
        }
        report_and_exit(report_pipe[1], Stage::Exec,
                        executable_error);
    }

    (void)::close(report_pipe[1]);
    Report report;
    std::size_t received = 0;
    while (received < sizeof(report)) {
        const auto moved = ::read(report_pipe[0],
                                  reinterpret_cast<char*>(&report) + received,
                                  sizeof(report) - received);
        if (moved < 0 && errno == EINTR) continue;
        if (moved <= 0) break;
        received += static_cast<std::size_t>(moved);
    }
    (void)::close(report_pipe[0]);

    if (received == 0) {
        out = self.publish_child(child);
        if (native.kind == NativeKind::Native && !pipeline_shell) {
            ++self.native_started_;
        }
        return full::ServiceStatus::Ok;
    }
    if (received != sizeof(report)) {
        int discarded = 0;
        (void)::waitpid(child, &discarded, 0);
        return full::ServiceStatus::Failed;
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
