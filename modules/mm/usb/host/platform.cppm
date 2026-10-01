// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.usb.host:platform;

import mm.usb;

export namespace mm::usb::host {

struct DeviceInfo {
    std::uint8_t bus = 0;
    std::uint8_t address = 0;
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;
    Speed speed = Speed::Unknown;
    constexpr bool operator==(const DeviceInfo&) const = default;
};

enum class EventKind { None, Attached, Detached };

struct Event {
    EventKind kind = EventKind::None;
    DeviceInfo device;
    constexpr bool operator==(const Event&) const = default;
};

// An open device, a small handle the provider gives meaning to.
struct Handle {
    unsigned int value = 0;
    constexpr bool operator==(const Handle&) const = default;
};

class Host {
public:
    virtual ~Host() = default;

    [[nodiscard]] virtual Status initialize() {
        return Status::Unsupported;
    }

    // The devices on the bus now, up to out's size, count in count; Ok with
    // zero when none. A query: it does not wait for devices to appear.
    [[nodiscard]] virtual Status list(std::span<DeviceInfo>, std::size_t&) {
        return Status::Unsupported;
    }

    // Attach and detach, latched in order; Ok with None when nothing waits.
    [[nodiscard]] virtual Status take_event(Event&) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status open(const DeviceInfo&, Handle&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status close(Handle) {
        return Status::Unsupported;
    }

    // Reads the device's descriptors into out: the device descriptor, then
    // the active configuration's.
    [[nodiscard]] virtual Status descriptors(Handle, std::span<std::byte>,
                                             std::size_t&) {
        return Status::Unsupported;
    }

    // Claims an interface for this program. A kernel or stack driver already
    // bound to it is detached only when the provider's configuration allows;
    // otherwise Busy.
    [[nodiscard]] virtual Status claim(Handle, unsigned int) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status release(Handle, unsigned int) {
        return Status::Unsupported;
    }

    // Transfers wait, up to timeout_ms, the bound docs/modules-execution.mdy
    // asks the caller to own: a control request with its data stage, and bulk
    // or interrupt transfers on an endpoint of a claimed interface. Timeout
    // when the bound passed before the transfer finished, with what did move
    // reported in transferred; Ok otherwise.
    [[nodiscard]] virtual Status control(Handle, const SetupPacket&,
                                         std::span<std::byte>,
                                         std::size_t&,
                                         unsigned long) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status transfer_out(Handle, EndpointAddress,
                                              std::span<const std::byte>,
                                              std::size_t&,
                                              unsigned long) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status transfer_in(Handle, EndpointAddress,
                                             std::span<std::byte>,
                                             std::size_t&,
                                             unsigned long) {
        return Status::Unsupported;
    }
};

void set_host(Host& host);
[[nodiscard]] Host& selected_host();

}
