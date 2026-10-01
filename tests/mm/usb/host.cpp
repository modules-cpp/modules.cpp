// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import mm.usb;
import mm.usb.host;
import mm.test;

namespace {

using mm::test::expect;
using mm::usb::EndpointAddress;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::host::DeviceInfo;
using mm::usb::host::Event;
using mm::usb::host::EventKind;
using mm::usb::host::Handle;
using mm::usb::host::Host;
using mm::usb::host::selected_host;
using mm::usb::host::set_host;

void fallback_answers_unsupported() {
    Host bare;
    expect(bare.initialize() == Status::Unsupported, "fallback initialize is Unsupported");

    std::array<DeviceInfo, 4> devs{};
    std::size_t dev_count = 111;
    expect(bare.list(devs, dev_count) == Status::Unsupported, "fallback list is Unsupported");
    expect(dev_count == 111, "fallback list leaves count unchanged on failure");

    Event event;
    expect(bare.take_event(event) == Status::Unsupported, "fallback take_event is Unsupported");

    Handle handle{999};
    expect(bare.open(DeviceInfo{}, handle) == Status::Unsupported, "fallback open is Unsupported");
    expect(handle.value == 999, "fallback open leaves handle unchanged on failure");

    expect(bare.close(Handle{1}) == Status::Unsupported, "fallback close is Unsupported");

    std::array<std::byte, 16> buffer{};
    std::size_t desc_count = 222;
    expect(bare.descriptors(Handle{1}, buffer, desc_count) == Status::Unsupported,
           "fallback descriptors is Unsupported");
    expect(desc_count == 222, "fallback descriptors leaves count unchanged on failure");

    expect(bare.claim(Handle{1}, 0) == Status::Unsupported, "fallback claim is Unsupported");
    expect(bare.release(Handle{1}, 0) == Status::Unsupported, "fallback release is Unsupported");

    std::size_t transferred = 333;
    expect(bare.control(Handle{1}, SetupPacket{}, buffer, transferred, 100) == Status::Unsupported,
           "fallback control is Unsupported");
    expect(transferred == 333, "fallback control leaves count unchanged on failure");

    std::size_t out_transferred = 444;
    expect(bare.transfer_out(Handle{1}, EndpointAddress{0x01}, buffer, out_transferred, 100) ==
               Status::Unsupported,
           "fallback transfer_out is Unsupported");
    expect(out_transferred == 444, "fallback transfer_out leaves count unchanged on failure");

    std::size_t in_transferred = 555;
    expect(bare.transfer_in(Handle{1}, EndpointAddress{0x81}, buffer, in_transferred, 100) ==
               Status::Unsupported,
           "fallback transfer_in is Unsupported");
    expect(in_transferred == 555, "fallback transfer_in leaves count unchanged on failure");
}

class RecordingHost final : public Host {
public:
    [[nodiscard]] Status initialize() override {
        initialized_ = true;
        return Status::Ok;
    }

    void add_device(const DeviceInfo& info) {
        devices_.push_back(info);
    }

    void inject_event(const Event& ev) {
        event_queue_.push_back(ev);
    }

    [[nodiscard]] Status list(std::span<DeviceInfo> out, std::size_t& count) override {
        const auto n = std::min(out.size(), devices_.size());
        std::copy_n(devices_.begin(), n, out.begin());
        count = n;
        return Status::Ok;
    }

    [[nodiscard]] Status take_event(Event& ev) override {
        if (event_queue_.empty()) {
            ev = Event{.kind = EventKind::None};
            return Status::Ok;
        }
        ev = event_queue_.front();
        event_queue_.erase(event_queue_.begin());
        return Status::Ok;
    }

    [[nodiscard]] Status open(const DeviceInfo& dev, Handle& handle) override {
        if (dev.address == 0) return Status::BadArgument;
        handle = Handle{next_handle_++};
        open_handles_.push_back(handle.value);
        return Status::Ok;
    }

    [[nodiscard]] Status close(Handle handle) override {
        auto it = std::find(open_handles_.begin(), open_handles_.end(), handle.value);
        if (it == open_handles_.end()) return Status::BadArgument;
        open_handles_.erase(it);
        return Status::Ok;
    }

    void set_descriptors_data(std::span<const std::byte> data) {
        descriptors_data_.assign(data.begin(), data.end());
    }

    [[nodiscard]] Status descriptors(Handle handle, std::span<std::byte> out,
                                     std::size_t& count) override {
        if (!is_open(handle)) return Status::BadArgument;
        const auto n = std::min(out.size(), descriptors_data_.size());
        std::copy_n(descriptors_data_.begin(), n, out.begin());
        count = n;
        return Status::Ok;
    }

    [[nodiscard]] Status claim(Handle handle, unsigned int iface) override {
        if (!is_open(handle)) return Status::BadArgument;
        claimed_interfaces_.push_back(iface);
        return Status::Ok;
    }

    [[nodiscard]] Status release(Handle handle, unsigned int iface) override {
        if (!is_open(handle)) return Status::BadArgument;
        auto it = std::find(claimed_interfaces_.begin(), claimed_interfaces_.end(), iface);
        if (it == claimed_interfaces_.end()) return Status::BadArgument;
        claimed_interfaces_.erase(it);
        return Status::Ok;
    }

    [[nodiscard]] Status control(Handle handle, const SetupPacket&,
                                 std::span<std::byte> data,
                                 std::size_t& transferred,
                                 unsigned long timeout_ms) override {
        if (!is_open(handle)) return Status::BadArgument;
        if (timeout_ms == 0) {
            transferred = 0;
            return Status::Timeout;
        }
        transferred = data.size();
        return Status::Ok;
    }

    [[nodiscard]] Status transfer_out(Handle handle, EndpointAddress ep,
                                      std::span<const std::byte> data,
                                      std::size_t& transferred,
                                      unsigned long timeout_ms) override {
        if (!is_open(handle) || !ep.valid() || ep.in()) return Status::BadArgument;
        if (timeout_ms == 0) {
            transferred = 0;
            return Status::Timeout;
        }
        transferred = data.size();
        return Status::Ok;
    }

    [[nodiscard]] Status transfer_in(Handle handle, EndpointAddress ep,
                                     std::span<std::byte> data,
                                     std::size_t& transferred,
                                     unsigned long timeout_ms) override {
        if (!is_open(handle) || !ep.valid() || !ep.in()) return Status::BadArgument;
        if (timeout_ms == 0) {
            transferred = 0;
            return Status::Timeout;
        }
        const auto n = std::min(data.size(), in_mock_data_.size());
        std::copy_n(in_mock_data_.begin(), n, data.begin());
        transferred = n;
        return Status::Ok;
    }

    void set_in_mock_data(std::span<const std::byte> data) {
        in_mock_data_.assign(data.begin(), data.end());
    }

    bool is_open(Handle h) const {
        return std::find(open_handles_.begin(), open_handles_.end(), h.value) !=
               open_handles_.end();
    }

    bool initialized_ = false;
    unsigned int next_handle_ = 1;
    std::vector<DeviceInfo> devices_;
    std::vector<Event> event_queue_;
    std::vector<unsigned int> open_handles_;
    std::vector<unsigned int> claimed_interfaces_;
    std::vector<std::byte> descriptors_data_;
    std::vector<std::byte> in_mock_data_;
};

RecordingHost recording_host;

void recording_host_lifecycle_and_discovery() {
    set_host(recording_host);
    expect(&selected_host() == &recording_host, "selected host is registered stand-in");

    expect(selected_host().initialize() == Status::Ok, "stand-in initialize");

    // Empty list returns Ok with count 0
    std::array<DeviceInfo, 4> list_buf{};
    std::size_t count = 99;
    expect(selected_host().list(list_buf, count) == Status::Ok, "list Ok on empty");
    expect(count == 0, "empty list count is 0");

    // Add device and list
    DeviceInfo dev{.bus = 1, .address = 2, .vendor = 0x1234, .product = 0x5678, .speed = Speed::High};
    recording_host.add_device(dev);
    expect(selected_host().list(list_buf, count) == Status::Ok, "list Ok with device");
    expect(count == 1, "device count is 1");
    expect(list_buf[0].vendor == 0x1234 && list_buf[0].speed == Speed::High, "device fields match");

    // Events
    Event ev;
    expect(selected_host().take_event(ev) == Status::Ok && ev.kind == EventKind::None,
           "empty event queue returns None");

    recording_host.inject_event(Event{.kind = EventKind::Attached, .device = dev});
    expect(selected_host().take_event(ev) == Status::Ok && ev.kind == EventKind::Attached,
           "take_event pops Attached event");
    expect(ev.device.product == 0x5678, "event DeviceInfo product matches");
}

void device_session_and_transfers() {
    set_host(recording_host);

    Handle handle{};
    DeviceInfo invalid_dev{.address = 0};
    // Open failure leaves handle unchanged
    handle.value = 777;
    expect(selected_host().open(invalid_dev, handle) == Status::BadArgument,
           "open with address 0 fails");
    expect(handle.value == 777, "failed open leaves handle unchanged");

    DeviceInfo dev{.bus = 1, .address = 2};
    expect(selected_host().open(dev, handle) == Status::Ok, "open valid device");
    expect(handle.value != 777, "open returns new valid handle");

    // Interface claim and release
    expect(selected_host().claim(handle, 0) == Status::Ok, "claim interface 0");
    expect(selected_host().release(handle, 0) == Status::Ok, "release interface 0");
    expect(selected_host().release(handle, 0) == Status::BadArgument,
           "re-releasing unassigned interface fails");

    // Bounded control transfer
    std::array<std::byte, 8> ctrl_data{};
    std::size_t transferred = 0;
    expect(selected_host().control(handle, SetupPacket{}, ctrl_data, transferred, 500) ==
               Status::Ok,
           "control transfer with timeout succeeds");
    expect(transferred == ctrl_data.size(), "transferred matches data size");

    // Bounded transfer timeout
    expect(selected_host().control(handle, SetupPacket{}, ctrl_data, transferred, 0) ==
               Status::Timeout,
           "control transfer with 0 timeout reports Timeout");

    // transfer_out
    const std::array out_bytes{std::byte{0xaa}, std::byte{0xbb}};
    expect(selected_host().transfer_out(handle, EndpointAddress{0x01}, out_bytes, transferred, 100) ==
               Status::Ok,
           "transfer_out succeeds");
    expect(transferred == 2, "transferred 2 bytes");

    // transfer_in
    const std::array mock_in{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    recording_host.set_in_mock_data(mock_in);
    std::array<std::byte, 8> in_buf{};
    expect(selected_host().transfer_in(handle, EndpointAddress{0x81}, in_buf, transferred, 100) ==
               Status::Ok,
           "transfer_in succeeds");
    expect(transferred == 3, "transferred 3 bytes");
    expect(in_buf[0] == std::byte{0x10} && in_buf[2] == std::byte{0x30}, "received data matches");

    // Failures leave transferred count unchanged
    transferred = 888;
    expect(selected_host().transfer_out(handle, EndpointAddress{0x81}, out_bytes, transferred, 100) ==
               Status::BadArgument,
           "transfer_out with IN endpoint is BadArgument");
    expect(transferred == 888, "failed transfer_out leaves transferred unchanged");

    transferred = 999;
    expect(selected_host().transfer_in(handle, EndpointAddress{0x01}, in_buf, transferred, 100) ==
               Status::BadArgument,
           "transfer_in with OUT endpoint is BadArgument");
    expect(transferred == 999, "failed transfer_in leaves transferred unchanged");

    // Close session
    expect(selected_host().close(handle) == Status::Ok, "close handle");
    expect(selected_host().close(handle) == Status::BadArgument, "closing again fails");
}

const mm::test::case_ cases[] = {
    {"fallback answers Unsupported", &fallback_answers_unsupported},
    {"recording host lifecycle and discovery", &recording_host_lifecycle_and_discovery},
    {"device session and transfers", &device_session_and_transfers},
};

const mm::test::registrar reg{"mm.usb.host", cases};

}  // namespace
