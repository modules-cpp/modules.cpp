// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.usb.device:platform;

import mm.usb;

export namespace mm::usb::device {

// What the program presents. Every span is the caller's and must outlive the
// device's use of it. strings[0] is the language list; strings[i] is string
// descriptor i. Each is a complete descriptor, header included.
struct Descriptors {
    std::span<const std::byte> device;
    std::span<const std::byte> configuration;   // one configuration
    std::span<const std::span<const std::byte>> strings;
};

enum class State { Detached, Default, Addressed, Configured, Suspended };

enum class EventKind {
    None,          // nothing happened: take_event answers Ok with this
    Reset,         // bus reset; every endpoint is closed
    Configured,    // SET_CONFIGURATION with a nonzero value; endpoints open
    Deconfigured,  // SET_CONFIGURATION 0, or detach
    Suspended,
    Resumed,
    Setup          // a class, vendor, or interface request to answer
};

struct Event {
    EventKind kind = EventKind::None;
    SetupPacket setup;   // meaningful for Setup
    constexpr bool operator==(const Event&) const = default;
};

class Device {
public:
    virtual ~Device() = default;

    // Hands the provider the descriptors and prepares the controller. The
    // configuration descriptor decides the endpoints; nothing opens them
    // separately. A provider that cannot present them (an endpoint its
    // controller lacks, a device descriptor the platform owns) answers
    // BadArgument or Unsupported and names nothing it half-applied.
    [[nodiscard]] virtual Status initialize(const Descriptors&) {
        return Status::Unsupported;
    }

    // Connects to and disconnects from the bus (the pull-up, or binding the
    // gadget to its controller).
    [[nodiscard]] virtual Status attach() {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status detach() {
        return Status::Unsupported;
    }

    // Queries: never wait.
    [[nodiscard]] virtual State state() const {
        return State::Detached;
    }
    [[nodiscard]] virtual Speed speed() const {
        return Speed::Unknown;
    }

    // The event latch. Answers Ok with EventKind::None when nothing is
    // waiting. Events are kept in order; a provider whose queue overflows
    // answers TransportError once and drops the oldest.
    [[nodiscard]] virtual Status take_event(Event&) {
        return Status::Unsupported;
    }

    // Answers the Setup event last taken. For an IN request, data is the
    // reply, at most setup.length bytes; for an OUT request with a data
    // stage, control_receive first takes that data. control_stall refuses
    // the request. Exactly one of reply and stall ends each Setup; standard
    // requests (GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, and the rest)
    // are the provider's and never reach the program.
    [[nodiscard]] virtual Status control_receive(std::span<std::byte>,
                                                 std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status control_reply(std::span<const std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status control_stall() {
        return Status::Unsupported;
    }

    // Nonblocking transfers on an endpoint the configuration opened. write
    // queues what the endpoint has room for and reports it in accepted; read
    // takes what has arrived and reports it in received; both answer Ok with
    // zero when there is no room or nothing waiting. NotInitialized before
    // Configured; BadArgument for an endpoint the configuration lacks or of
    // the wrong direction. On failure the counts are unchanged.
    [[nodiscard]] virtual Status write(EndpointAddress, std::span<const std::byte>,
                                       std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status read(EndpointAddress, std::span<std::byte>,
                                      std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status stall(EndpointAddress, bool) {
        return Status::Unsupported;
    }
};

void set_device(Device& device);
[[nodiscard]] Device& selected_device();

}
