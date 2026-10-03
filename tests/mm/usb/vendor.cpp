// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

import mm.usb;
import mm.usb.device;
import mm.usb.host;
import mm.usb.vendor;
import mm.test;

namespace {

using mm::test::expect;
using mm::usb::Direction;
using mm::usb::EndpointAddress;
using mm::usb::Recipient;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::Type;
using mm::usb::device::Descriptors;
using mm::usb::device::Device;
using mm::usb::device::Event;
using mm::usb::device::EventKind;
using mm::usb::device::State;
using mm::usb::host::DeviceInfo;
using mm::usb::host::Handle;
using mm::usb::host::Host;

void test_descriptors() {
    expect(mm::usb::vendor::VENDOR_ID == 0x1d50, "Vendor ID is 0x1d50");
    expect(mm::usb::vendor::PRODUCT_ID == 0x6150, "Product ID is 0x6150");
    expect(mm::usb::vendor::ENDPOINT_BULK_OUT.value == 0x01, "EP bulk out is 0x01");
    expect(mm::usb::vendor::ENDPOINT_BULK_IN.value == 0x81, "EP bulk in is 0x81");

    const auto descs = mm::usb::vendor::default_descriptors();
    expect(descs.device.size() == 18, "Device descriptor is 18 bytes");
    // Verify VID / PID in descriptor
    const uint16_t vid = static_cast<uint8_t>(descs.device[8]) |
                        (static_cast<uint8_t>(descs.device[9]) << 8);
    const uint16_t pid = static_cast<uint8_t>(descs.device[10]) |
                        (static_cast<uint8_t>(descs.device[11]) << 8);
    expect(vid == 0x1d50, "Descriptor VID is 0x1d50");
    expect(pid == 0x6150, "Descriptor PID is 0x6150");

    expect(descs.configuration.size() == 32, "Configuration descriptor is 32 bytes");
    expect(descs.strings.size() == 4, "Strings list has 4 entries");
}

class MockDevice final : public Device {
public:
    [[nodiscard]] Status initialize(const Descriptors& d) override {
        initialized_ = true;
        received_descs_ = d;
        state_ = State::Default;
        return Status::Ok;
    }

    [[nodiscard]] Status attach() override {
        attached_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status detach() override {
        attached_ = false;
        state_ = State::Detached;
        return Status::Ok;
    }

    [[nodiscard]] State state() const override { return state_; }
    void set_state(State s) { state_ = s; }

    void inject_event(const Event& ev) {
        events_.push_back(ev);
    }

    [[nodiscard]] Status take_event(Event& ev) override {
        if (events_.empty()) {
            ev = Event{.kind = EventKind::None};
            return Status::Ok;
        }
        ev = events_.front();
        events_.erase(events_.begin());
        return Status::Ok;
    }

    [[nodiscard]] Status control_reply(std::span<const std::byte> data) override {
        replied_ = true;
        reply_data_.assign(data.begin(), data.end());
        return Status::Ok;
    }

    [[nodiscard]] Status control_stall() override {
        stalled_ = true;
        return Status::Ok;
    }

    void inject_out_data(std::span<const std::byte> data) {
        out_fifo_.insert(out_fifo_.end(), data.begin(), data.end());
    }

    [[nodiscard]] Status read(EndpointAddress ep, std::span<std::byte> buf,
                              std::size_t& transferred) override {
        if (ep != mm::usb::vendor::ENDPOINT_BULK_OUT) return Status::BadArgument;
        if (out_fifo_.empty()) {
            transferred = 0;
            return Status::Ok;
        }
        const std::size_t count = std::min(buf.size(), out_fifo_.size());
        std::copy_n(out_fifo_.begin(), count, buf.begin());
        out_fifo_.erase(out_fifo_.begin(), out_fifo_.begin() + count);
        transferred = count;
        return Status::Ok;
    }

    [[nodiscard]] Status write(EndpointAddress ep, std::span<const std::byte> buf,
                               std::size_t& transferred) override {
        if (ep != mm::usb::vendor::ENDPOINT_BULK_IN) return Status::BadArgument;
        in_fifo_.insert(in_fifo_.end(), buf.begin(), buf.end());
        transferred = buf.size();
        return Status::Ok;
    }

    bool initialized_ = false;
    bool attached_ = false;
    bool replied_ = false;
    bool stalled_ = false;
    State state_ = State::Detached;
    Descriptors received_descs_;
    std::vector<Event> events_;
    std::vector<std::byte> reply_data_;
    std::vector<std::byte> out_fifo_;
    std::vector<std::byte> in_fifo_;
};

void test_device_echo() {
    MockDevice dev;
    mm::usb::vendor::DeviceEcho service(dev);

    expect(service.initialize() == Status::Ok, "DeviceEcho initialize ok");
    expect(dev.initialized_, "MockDevice initialized");
    expect(dev.received_descs_.device.size() == 18, "Received device descriptors");

    expect(service.attach() == Status::Ok, "DeviceEcho attach ok");
    expect(dev.attached_, "MockDevice attached");

    // Test vendor control request handling
    const SetupPacket vendor_setup{
        .request_type = 0xc0, // IN, Vendor, Device
        .request = mm::usb::vendor::VENDOR_REQUEST_ECHO,
        .value = 0,
        .index = 0,
        .length = 4
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = vendor_setup});
    expect(service.poll() == Status::Ok, "poll setup event ok");
    expect(dev.replied_, "vendor setup answered with reply");
    expect(dev.reply_data_.size() == 4, "reply data length 4");
    expect(dev.reply_data_[0] == std::byte{0xaa} && dev.reply_data_[1] == std::byte{0x55},
           "vendor reply prefix 0xaa 0x55");

    // Test non-vendor setup stalls
    dev.replied_ = false;
    dev.stalled_ = false;
    const SetupPacket std_setup{
        .request_type = 0x00, // OUT, Standard, Device
        .request = 0x05,      // SET_ADDRESS
        .value = 42,
        .index = 0,
        .length = 0
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = std_setup});
    expect(service.poll() == Status::Ok, "poll standard setup event ok");
    expect(dev.stalled_, "non-vendor setup stalled");

    // Test bulk echo
    dev.set_state(State::Configured);
    const std::array<std::byte, 8> test_payload = {
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}
    };
    dev.inject_out_data(test_payload);

    expect(service.poll() == Status::Ok, "poll bulk read/write ok");
    expect(dev.in_fifo_.size() == 8, "8 bytes echoed to bulk in");
    expect(std::memcmp(dev.in_fifo_.data(), test_payload.data(), 8) == 0, "echo payload matches");
    expect(service.total_echoed() == 8, "total_echoed is 8");

    expect(service.detach() == Status::Ok, "DeviceEcho detach ok");
}

class MockHost final : public Host {
public:
    [[nodiscard]] Status initialize() override {
        initialized_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status list(std::span<DeviceInfo> out, std::size_t& count) override {
        if (!has_device_) {
            count = 0;
            return Status::Ok;
        }
        if (out.empty()) return Status::BadArgument;
        out[0] = DeviceInfo{
            .bus = 1,
            .address = 2,
            .vendor = mm::usb::vendor::VENDOR_ID,
            .product = mm::usb::vendor::PRODUCT_ID,
            .speed = Speed::High
        };
        count = 1;
        return Status::Ok;
    }

    [[nodiscard]] Status open(const DeviceInfo& dev, Handle& handle) override {
        if (dev.vendor != mm::usb::vendor::VENDOR_ID) return Status::BadArgument;
        handle = Handle{42};
        opened_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status close(Handle handle) override {
        if (handle.value != 42) return Status::BadArgument;
        opened_ = false;
        return Status::Ok;
    }

    [[nodiscard]] Status claim(Handle handle, unsigned int iface) override {
        if (handle.value != 42 || iface != 0) return Status::BadArgument;
        claimed_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status release(Handle handle, unsigned int iface) override {
        if (handle.value != 42 || iface != 0) return Status::BadArgument;
        claimed_ = false;
        return Status::Ok;
    }

    [[nodiscard]] Status control(Handle handle, const SetupPacket& setup,
                                 std::span<std::byte> data, std::size_t& transferred,
                                 unsigned long) override {
        if (handle.value != 42) return Status::BadArgument;
        if (setup.type() == Type::Vendor && setup.request == mm::usb::vendor::VENDOR_REQUEST_ECHO) {
            const std::array<std::byte, 4> resp = {
                std::byte{0xaa}, std::byte{0x55}, std::byte{0x08}, std::byte{0x00}
            };
            const std::size_t n = std::min(data.size(), resp.size());
            std::copy_n(resp.begin(), n, data.begin());
            transferred = n;
            return Status::Ok;
        }
        return Status::BadArgument;
    }

    [[nodiscard]] Status transfer_out(Handle handle, EndpointAddress ep,
                                      std::span<const std::byte> data,
                                      std::size_t& transferred,
                                      unsigned long) override {
        if (handle.value != 42 || ep != mm::usb::vendor::ENDPOINT_BULK_OUT) return Status::BadArgument;
        out_buf_.assign(data.begin(), data.end());
        transferred = data.size();
        return Status::Ok;
    }

    [[nodiscard]] Status transfer_in(Handle handle, EndpointAddress ep,
                                     std::span<std::byte> data,
                                     std::size_t& transferred,
                                     unsigned long) override {
        if (handle.value != 42 || ep != mm::usb::vendor::ENDPOINT_BULK_IN) return Status::BadArgument;
        const std::size_t n = std::min(data.size(), out_buf_.size());
        std::copy_n(out_buf_.begin(), n, data.begin());
        transferred = n;
        return Status::Ok;
    }

    bool initialized_ = false;
    bool has_device_ = true;
    bool opened_ = false;
    bool claimed_ = false;
    std::vector<std::byte> out_buf_;
};

void test_host_echo() {
    MockHost host;
    mm::usb::vendor::HostEcho client(host);

    Handle h;
    expect(client.find_and_open(h) == Status::Ok, "find_and_open ok");
    expect(host.opened_, "host opened");
    expect(host.claimed_, "host claimed interface 0");

    const std::array<std::byte, 8> out_data = {
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
        std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80}
    };
    std::array<std::byte, 8> in_data{};
    std::size_t transferred = 0;

    expect(client.echo(h, out_data, in_data, transferred) == Status::Ok, "echo transaction ok");
    expect(transferred == 8, "echo transferred 8 bytes");
    expect(std::memcmp(out_data.data(), in_data.data(), 8) == 0, "echoed data matches");

    std::array<std::byte, 4> ctrl_buf{};
    std::size_t ctrl_len = 0;
    expect(client.vendor_control(h, mm::usb::vendor::VENDOR_REQUEST_ECHO, 0x1234,
                                 ctrl_buf, ctrl_len) == Status::Ok,
           "vendor_control ok");
    expect(ctrl_len == 4, "vendor control returned 4 bytes");
    expect(ctrl_buf[0] == std::byte{0xaa} && ctrl_buf[1] == std::byte{0x55},
           "vendor control response signature 0xaa 0x55");

    expect(client.close(h) == Status::Ok, "client close ok");
    expect(!host.opened_, "host closed");
    expect(!host.claimed_, "host interface released");
}

const mm::test::case_ cases[] = {
    {"vendor descriptors", &test_descriptors},
    {"device echo service", &test_device_echo},
    {"host echo client", &test_host_echo},
};

const mm::test::registrar reg{"mm.usb.vendor", cases};

}  // namespace
