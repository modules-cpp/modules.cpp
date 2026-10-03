// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

import mm.stdio;
import mm.usb;
import mm.usb.device;
import mm.usb.cdc;
import mm.test;

namespace {

using mm::test::expect;
using mm::usb::Direction;
using mm::usb::EndpointAddress;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::Type;
using mm::usb::device::Descriptors;
using mm::usb::device::Device;
using mm::usb::device::Event;
using mm::usb::device::EventKind;
using mm::usb::device::State;
using mm::usb::cdc::LineCoding;

void test_descriptors_and_line_coding() {
    LineCoding lc{.bitrate = 115200, .stopbits = 0, .parity = 0, .databits = 8};
    auto enc = mm::usb::cdc::encode_line_coding(lc);
    expect(enc.size() == 7, "encoded line coding is 7 bytes");

    LineCoding dec{};
    expect(mm::usb::cdc::decode_line_coding(enc, dec), "decode line coding ok");
    expect(dec == lc, "decoded line coding matches");

    const auto descs = mm::usb::cdc::default_descriptors();
    expect(descs.device.size() == 18, "device descriptor is 18 bytes");
    expect(descs.configuration.size() == 75, "configuration descriptor is 75 bytes");
    expect(descs.strings.size() == 4, "strings list has 4 entries");
}

class MockCdcDevice final : public Device {
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

    void inject_control_data(std::span<const std::byte> data) {
        ctrl_rx_fifo_.assign(data.begin(), data.end());
    }

    [[nodiscard]] Status control_receive(std::span<std::byte> buf, std::size_t& received) override {
        const size_t n = std::min(buf.size(), ctrl_rx_fifo_.size());
        std::copy_n(ctrl_rx_fifo_.begin(), n, buf.begin());
        ctrl_rx_fifo_.erase(ctrl_rx_fifo_.begin(), ctrl_rx_fifo_.begin() + n);
        received = n;
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

    void inject_bulk_out_data(std::span<const std::byte> data) {
        bulk_out_fifo_.insert(bulk_out_fifo_.end(), data.begin(), data.end());
    }

    [[nodiscard]] Status read(EndpointAddress ep, std::span<std::byte> buf,
                              std::size_t& transferred) override {
        if (ep != mm::usb::cdc::ENDPOINT_BULK_OUT) return Status::BadArgument;
        if (bulk_out_fifo_.empty()) {
            transferred = 0;
            return Status::Ok;
        }
        const std::size_t n = std::min(buf.size(), bulk_out_fifo_.size());
        std::copy_n(bulk_out_fifo_.begin(), n, buf.begin());
        bulk_out_fifo_.erase(bulk_out_fifo_.begin(), bulk_out_fifo_.begin() + n);
        transferred = n;
        return Status::Ok;
    }

    [[nodiscard]] Status write(EndpointAddress ep, std::span<const std::byte> buf,
                               std::size_t& transferred) override {
        if (ep != mm::usb::cdc::ENDPOINT_BULK_IN) return Status::BadArgument;
        bulk_in_fifo_.insert(bulk_in_fifo_.end(), buf.begin(), buf.end());
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
    std::vector<std::byte> ctrl_rx_fifo_;
    std::vector<std::byte> reply_data_;
    std::vector<std::byte> bulk_out_fifo_;
    std::vector<std::byte> bulk_in_fifo_;
};

void test_cdc_device_lifecycle_and_requests() {
    MockCdcDevice dev;
    mm::usb::cdc::CdcDevice cdc(dev);

    expect(cdc.initialize() == Status::Ok, "CdcDevice initialize ok");
    expect(cdc.attach() == Status::Ok, "CdcDevice attach ok");

    // SET_CONTROL_LINE_STATE: DTR (bit 0) + RTS (bit 1)
    const SetupPacket set_cls{
        .request_type = 0x21, // Class, Interface, Out
        .request = mm::usb::cdc::CDC_REQUEST_SET_CONTROL_LINE_STATE,
        .value = 0x03, // DTR = 1, RTS = 1
        .index = 0,
        .length = 0
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = set_cls});
    expect(cdc.poll() == Status::Ok, "poll SET_CONTROL_LINE_STATE ok");
    expect(dev.replied_, "SET_CONTROL_LINE_STATE replied");
    expect(cdc.dtr(), "DTR is true");
    expect(cdc.rts(), "RTS is true");

    // SET_LINE_CODING: 9600 baud, 8N1
    LineCoding new_lc{.bitrate = 9600, .stopbits = 0, .parity = 0, .databits = 8};
    auto enc = mm::usb::cdc::encode_line_coding(new_lc);
    dev.inject_control_data(enc);
    dev.replied_ = false;

    const SetupPacket set_lc{
        .request_type = 0x21, // Class, Interface, Out
        .request = mm::usb::cdc::CDC_REQUEST_SET_LINE_CODING,
        .value = 0,
        .index = 0,
        .length = 7
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = set_lc});
    expect(cdc.poll() == Status::Ok, "poll SET_LINE_CODING ok");
    expect(dev.replied_, "SET_LINE_CODING replied");
    expect(cdc.line_coding().bitrate == 9600, "bitrate updated to 9600");

    // GET_LINE_CODING
    dev.replied_ = false;
    const SetupPacket get_lc{
        .request_type = 0xa1, // Class, Interface, In
        .request = mm::usb::cdc::CDC_REQUEST_GET_LINE_CODING,
        .value = 0,
        .index = 0,
        .length = 7
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = get_lc});
    expect(cdc.poll() == Status::Ok, "poll GET_LINE_CODING ok");
    expect(dev.replied_, "GET_LINE_CODING replied");
    expect(dev.reply_data_.size() == 7, "reply length is 7");
    LineCoding returned_lc{};
    expect(mm::usb::cdc::decode_line_coding(dev.reply_data_, returned_lc), "decode returned lc ok");
    expect(returned_lc == new_lc, "returned line coding matches");

    // Bulk transfer when Configured
    dev.set_state(State::Configured);
    expect(cdc.connected(), "connected is true when Configured and DTR is set");

    const std::array<std::byte, 4> tx_bytes = {std::byte{'t'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}};
    std::size_t written = 0;
    expect(cdc.write(tx_bytes, written) == Status::Ok, "write ok");
    expect(written == 4, "4 bytes written");
    expect(dev.bulk_in_fifo_.size() == 4, "bulk in received 4 bytes");

    dev.inject_bulk_out_data(tx_bytes);
    std::array<std::byte, 4> rx_bytes{};
    std::size_t read_count = 0;
    expect(cdc.read(rx_bytes, read_count) == Status::Ok, "read ok");
    expect(read_count == 4, "4 bytes read");
    expect(std::memcmp(rx_bytes.data(), tx_bytes.data(), 4) == 0, "read data matches");

    expect(cdc.detach() == Status::Ok, "detach ok");
}

void test_cdc_console_provider() {
    MockCdcDevice dev;
    mm::usb::cdc::CdcDevice cdc(dev);
    mm::usb::cdc::CdcConsole console(cdc);

    // Prior to initialize
    std::size_t written = 999;
    std::size_t read_count = 999;
    bool conn = true;
    const std::array<std::byte, 4> msg = {std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
    std::array<std::byte, 4> rx_buf{};

    expect(console.write(msg, written) == mm::stdio::Status::NotInitialized, "uninitialized write refused");
    expect(console.read(rx_buf, read_count) == mm::stdio::Status::NotInitialized, "uninitialized read refused");
    expect(console.connected(conn) == mm::stdio::Status::NotInitialized, "uninitialized connected refused");
    expect(console.flush() == mm::stdio::Status::NotInitialized, "uninitialized flush refused");

    // Initialize
    expect(console.initialize() == mm::stdio::Status::Ok, "console initialize ok");
    expect(dev.initialized_, "mock device initialized");
    expect(dev.attached_, "mock device attached");

    // Connected query before configuration & DTR
    expect(console.connected(conn) == mm::stdio::Status::Ok, "connected query ok");
    expect(!conn, "not connected before config/dtr");

    // Disconnected write reports Ok with count 0
    written = 999;
    expect(console.write(msg, written) == mm::stdio::Status::Ok, "disconnected write returns Ok");
    expect(written == 0, "disconnected write accepted 0 bytes");

    // Disconnected read reports Ok with count 0
    read_count = 999;
    expect(console.read(rx_buf, read_count) == mm::stdio::Status::Ok, "disconnected read returns Ok");
    expect(read_count == 0, "disconnected read returned 0 bytes");

    // Configure and set DTR
    dev.set_state(State::Configured);
    const SetupPacket set_cls{
        .request_type = 0x21,
        .request = mm::usb::cdc::CDC_REQUEST_SET_CONTROL_LINE_STATE,
        .value = 0x01, // DTR = 1
        .index = 0,
        .length = 0
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = set_cls});
    expect(console.connected(conn) == mm::stdio::Status::Ok, "connected query ok after dtr");
    expect(conn, "connected is now true");

    // Connected write
    written = 0;
    expect(console.write(msg, written) == mm::stdio::Status::Ok, "connected write returns Ok");
    expect(written == 4, "connected write wrote 4 bytes");
    expect(dev.bulk_in_fifo_.size() == 4, "device bulk in fifo has 4 bytes");

    // Connected read when empty
    read_count = 999;
    expect(console.read(rx_buf, read_count) == mm::stdio::Status::Ok, "connected empty read returns Ok");
    expect(read_count == 0, "connected empty read returned 0 bytes");

    // Connected read with data
    dev.inject_bulk_out_data(msg);
    read_count = 0;
    expect(console.read(rx_buf, read_count) == mm::stdio::Status::Ok, "connected read with data returns Ok");
    expect(read_count == 4, "connected read returned 4 bytes");
    expect(std::memcmp(rx_buf.data(), msg.data(), 4) == 0, "read data matches written message");

    // Flush
    expect(console.flush() == mm::stdio::Status::Ok, "flush returns Ok");

    // Test mm::stdio console registration
    mm::stdio::set_console(console);
    expect(&mm::stdio::selected_console() == &console, "selected_console is our CdcConsole");
}

const mm::test::case_ cases[] = {
    {"descriptors and line coding", &test_descriptors_and_line_coding},
    {"cdc device lifecycle and requests", &test_cdc_device_lifecycle_and_requests},
    {"cdc console provider for mm.stdio", &test_cdc_console_provider},
};

const mm::test::registrar reg{"mm.usb.cdc", cases};

}  // namespace
