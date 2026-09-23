// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.posix:service;

import mm.shell.full;

export namespace mm::shell::posix {

// Owns every host resource the abstract services hand out: open descriptors,
// live children, and installed signal dispositions. Destruction closes and
// restores whatever it still owns, so an abandoned command leaks nothing.
//
// A handle is an index into this object's own tables rather than a descriptor
// or a process id, so no POSIX type appears in any exported declaration. The
// tables hold long because a descriptor and a process id both fit in one on
// every POSIX host, which keeps the header free of <sys/types.h>.
//
// Signal dispositions are process-wide, so at most one instance may install
// them at a time. The instance that installed a disposition restores it.
class HostServices {
public:
    HostServices() = default;
    ~HostServices();
    HostServices(const HostServices&) = delete;
    HostServices& operator=(const HostServices&) = delete;
    HostServices(HostServices&&) = delete;
    HostServices& operator=(HostServices&&) = delete;

    [[nodiscard]] full::IoService io();
    [[nodiscard]] full::ProcessService process();
    [[nodiscard]] full::SignalService signal();
    [[nodiscard]] full::FileService file();
    [[nodiscard]] full::Services all();

    // What this object still owns, for a leak assertion after a failure.
    [[nodiscard]] std::size_t open_descriptors() const;
    [[nodiscard]] std::size_t live_children() const;
    [[nodiscard]] std::size_t installed_signals() const;

    // Adopts an already-open descriptor this object did not open, such as the
    // process's own standard streams, without taking ownership of closing it.
    [[nodiscard]] full::Handle borrow_descriptor(int descriptor);

    void close_all();
    void restore_all_signals();

private:
    static full::ServiceStatus open_callback(void*, std::string_view,
                                             full::OpenMode,
                                             full::Handle&);
    static full::ServiceStatus pipe_callback(void*, full::Handle&,
                                             full::Handle&);
    static full::ServiceStatus read_callback(void*, full::Handle,
                                             std::span<std::byte>,
                                             std::size_t&);
    static full::ServiceStatus write_callback(void*, full::Handle,
                                              std::span<const std::byte>,
                                              std::size_t&);
    static full::ServiceStatus close_callback(void*, full::Handle);
    static full::ServiceStatus spawn_callback(void*,
                                              const full::ProcessRequest&,
                                              full::Handle&);
    static full::ServiceStatus wait_callback(void*, full::Handle, int&);
    static full::ServiceStatus kill_callback(void*, full::Handle, int);
    static full::ServiceStatus install_callback(void*, int);
    static full::ServiceStatus restore_callback(void*, int);
    static full::ServiceStatus poll_callback(void*, int&);
    static full::ServiceStatus list_callback(void*, std::string_view,
                                             std::vector<std::string>&);
    static full::ServiceStatus status_callback(void*, std::string_view,
                                               bool&, bool&);

    [[nodiscard]] full::Handle publish_descriptor(long descriptor,
                                                 bool owned);
    [[nodiscard]] long descriptor_of(full::Handle handle) const;
    void release_descriptor(full::Handle handle);
    [[nodiscard]] full::Handle publish_child(long child);
    [[nodiscard]] long child_of(full::Handle handle) const;
    void release_child(full::Handle handle);

    struct Descriptor {
        long value = -1;
        bool owned = true;
    };

    std::vector<Descriptor> descriptors_;
    std::vector<long> children_;
    std::vector<int> signals_;
};

}  // namespace mm::shell::posix
