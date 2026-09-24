// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:service;

export namespace mm::shell::full {

class FullState;
class FullFunctionLibrary;

enum class ServiceStatus {
    Ok, NotFound, PermissionDenied, Interrupted, WouldBlock,
    Invalid, Failed,
};

// Opaque handles and callback services keep POSIX types out of this module.
using Handle = unsigned long;
constexpr Handle invalid_handle = std::numeric_limits<Handle>::max();

enum class OpenMode { Read, Truncate, Append };

struct ProcessRequest {
    std::span<const std::string_view> arguments;
    std::span<const std::string_view> environment;
    std::string_view directory;
    Handle input = invalid_handle;
    Handle output = invalid_handle;
    Handle error = invalid_handle;
    // A pipeline may run a shell production in an isolated native child.
    // Both pointers are borrowed until spawn forks; no POSIX type crosses
    // this interface.
    std::string_view native_source;
    const FullState* native_state = nullptr;
    const FullFunctionLibrary* native_functions = nullptr;
};

struct IoService {
    void* context = nullptr;
    ServiceStatus (*open)(void*, std::string_view, OpenMode,
                          Handle&) = nullptr;
    ServiceStatus (*pipe)(void*, Handle&, Handle&) = nullptr;
    ServiceStatus (*read)(void*, Handle, std::span<std::byte>,
                          std::size_t&) = nullptr;
    ServiceStatus (*write)(void*, Handle, std::span<const std::byte>,
                           std::size_t&) = nullptr;
    ServiceStatus (*close)(void*, Handle) = nullptr;
};

struct ProcessService {
    void* context = nullptr;
    ServiceStatus (*spawn)(void*, const ProcessRequest&,
                           Handle&) = nullptr;
    ServiceStatus (*wait)(void*, Handle, int&) = nullptr;
    ServiceStatus (*signal)(void*, Handle, int) = nullptr;
};

struct SignalService {
    void* context = nullptr;
    ServiceStatus (*install)(void*, int) = nullptr;
    ServiceStatus (*restore)(void*, int) = nullptr;
    ServiceStatus (*poll)(void*, int&) = nullptr;
};

enum class FilePredicate {
    Exists, Regular, Directory, Readable, Writable, Executable,
    Nonempty, SymbolicLink,
};

// Directory enumeration is separate from byte I/O because pathname expansion
// needs names and nothing else. list appends the entries of one directory,
// without . and .., in whatever order the host reports them.
struct FileService {
    void* context = nullptr;
    ServiceStatus (*list)(void*, std::string_view,
                          std::vector<std::string>&) = nullptr;
    ServiceStatus (*status)(void*, std::string_view, bool&,
                            bool&) = nullptr;
    ServiceStatus (*canonical)(void*, std::string_view,
                               std::string&) = nullptr;
    ServiceStatus (*test)(void*, std::string_view,
                          FilePredicate, bool&) = nullptr;
};

struct Services {
    IoService io;
    ProcessService process;
    SignalService signal;
    FileService file;
};

}  // namespace mm::shell::full
