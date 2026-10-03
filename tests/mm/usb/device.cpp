// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import mm.usb;
import mm.usb.device;
import mm.test;

namespace {

using mm::test::expect;
using mm::usb::Direction;
using mm::usb::EndpointAddress;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::device::Descriptors;
using mm::usb::device::Device;
using mm::usb::device::Event;
using mm::usb::device::EventKind;
using mm::usb::device::State;
using mm::usb::device::selected_device;
using mm::usb::device::set_device;

void fallback_answers_unsupported() {
    Device bare;
    expect(bare.initialize({}) == Status::Unsupported, "fallback initialize is Unsupported");
    expect(bare.attach() == Status::Unsupported, "fallback attach is Unsupported");
    expect(bare.detach() == Status::Unsupported, "fallback detach is Unsupported");
    expect(bare.state() == State::Detached, "fallback state is Detached");
    expect(bare.speed() == Speed::Unknown, "fallback speed is Unknown");

    Event event;
    expect(bare.take_event(event) == Status::Unsupported, "fallback take_event is Unsupported");

    std::array<std::byte, 16> buffer{};
    std::size_t received = 123;
    expect(bare.control_receive(buffer, received) == Status::Unsupported,
           "fallback control_receive is Unsupported");
    expect(received == 123, "fallback control_receive leaves count unchanged on failure");

    expect(bare.control_reply(buffer) == Status::Unsupported,
           "fallback control_reply is Unsupported");
    expect(bare.control_stall() == Status::Unsupported,
           "fallback control_stall is Unsupported");

    std::size_t accepted = 456;
    expect(bare.write(EndpointAddress{0x01}, buffer, accepted) == Status::Unsupported,
           "fallback write is Unsupported");
    expect(accepted == 456, "fallback write leaves count unchanged on failure");

    std::size_t read_count = 789;
    expect(bare.read(EndpointAddress{0x81}, buffer, read_count) == Status::Unsupported,
           "fallback read is Unsupported");
    expect(read_count == 789, "fallback read leaves count unchanged on failure");

    expect(bare.stall(EndpointAddress{0x01}, true) == Status::Unsupported,
           "fallback stall is Unsupported");
}

class RecordingDevice final : public Device {
public:
    static constexpr std::size_t endpoint_capacity = 64;

    [[nodiscard]] Status initialize(const Descriptors&) override {
        initialized_ = true;
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
    [[nodiscard]] Speed speed() const override { return speed_; }

    void set_state(State s) { state_ = s; }
    void set_speed(Speed sp) { speed_ = sp; }

    void inject_event(const Event& ev) {
        event_queue_.push_back(ev);
    }

    [[nodiscard]] Status take_event(Event& ev) override {
        if (event_queue_.empty()) {
            ev = Event{.kind = EventKind::None};
            return Status::Ok;
        }
        ev = event_queue_.front();
        event_queue_.erase(event_queue_.begin());
        if (ev.kind == EventKind::Setup) {
            pending_setup_ = true;
            setup_answered_ = false;
            current_setup_ = ev.setup;
        }
        return Status::Ok;
    }

    void inject_control_data(std::span<const std::byte> data) {
        control_rx_buffer_.assign(data.begin(), data.end());
    }

    [[nodiscard]] Status control_receive(std::span<std::byte> out,
                                         std::size_t& received) override {
        if (!pending_setup_) return Status::BadArgument;
        const auto n = std::min(out.size(), control_rx_buffer_.size());
        std::copy_n(control_rx_buffer_.begin(), n, out.begin());
        control_rx_buffer_.erase(control_rx_buffer_.begin(), control_rx_buffer_.begin() + n);
        received = n;
        return Status::Ok;
    }

    [[nodiscard]] Status control_reply(std::span<const std::byte> data) override {
        if (!pending_setup_ || setup_answered_) {
            return Status::BadArgument;
        }
        setup_answered_ = true;
        pending_setup_ = false;
        reply_buffer_.assign(data.begin(), data.end());
        reply_count_++;
        return Status::Ok;
    }

    [[nodiscard]] Status control_stall() override {
        if (!pending_setup_ || setup_answered_) {
            return Status::BadArgument;
        }
        setup_answered_ = true;
        pending_setup_ = false;
        stall_count_++;
        return Status::Ok;
    }

    void queue_read_data(EndpointAddress ep, std::span<const std::byte> data) {
        auto& q = ep_buffers_[ep.number()];
        q.insert(q.end(), data.begin(), data.end());
    }

    void fill_write_buffer(EndpointAddress ep) {
        auto& q = ep_buffers_[ep.number()];
        q.resize(endpoint_capacity, std::byte{0xff});
    }

    [[nodiscard]] Status write(EndpointAddress ep, std::span<const std::byte> data,
                               std::size_t& accepted) override {
        if (!ep.valid() || ep.in()) return Status::BadArgument;
        if (state_ != State::Configured) return Status::NotInitialized;

        auto& q = ep_buffers_[ep.number()];
        if (q.size() >= endpoint_capacity) {
            accepted = 0;
            return Status::Ok;
        }
        const auto available = endpoint_capacity - q.size();
        const auto to_write = std::min(available, data.size());
        q.insert(q.end(), data.begin(), data.begin() + to_write);
        accepted = to_write;
        return Status::Ok;
    }

    [[nodiscard]] Status read(EndpointAddress ep, std::span<std::byte> out,
                              std::size_t& received) override {
        if (!ep.valid() || !ep.in()) return Status::BadArgument;
        if (state_ != State::Configured) return Status::NotInitialized;

        auto& q = ep_buffers_[ep.number()];
        if (q.empty()) {
            received = 0;
            return Status::Ok;
        }
        const auto to_read = std::min(out.size(), q.size());
        std::copy_n(q.begin(), to_read, out.begin());
        q.erase(q.begin(), q.begin() + to_read);
        received = to_read;
        return Status::Ok;
    }

    [[nodiscard]] Status stall(EndpointAddress ep, bool halted) override {
        if (!ep.valid()) return Status::BadArgument;
        ep_halted_[ep.number()] = halted;
        return Status::Ok;
    }

    bool initialized_ = false;
    bool attached_ = false;
    State state_ = State::Detached;
    Speed speed_ = Speed::Full;
    std::vector<Event> event_queue_;
    bool pending_setup_ = false;
    bool setup_answered_ = false;
    SetupPacket current_setup_;
    std::vector<std::byte> control_rx_buffer_;
    std::vector<std::byte> reply_buffer_;
    std::size_t reply_count_ = 0;
    std::size_t stall_count_ = 0;
    std::array<std::vector<std::byte>, 16> ep_buffers_;
    std::array<bool, 16> ep_halted_{};
};

RecordingDevice recording;

void recording_device_lifecycle_and_events() {
    set_device(recording);
    expect(&selected_device() == &recording, "selected device is registered stand-in");

    expect(selected_device().initialize({}) == Status::Ok, "stand-in initialize");
    expect(selected_device().attach() == Status::Ok, "stand-in attach");
    expect(recording.attached_, "stand-in attached");

    // Event queue returns None when empty
    Event ev;
    expect(selected_device().take_event(ev) == Status::Ok, "take_event Ok on empty");
    expect(ev.kind == EventKind::None, "empty queue produces EventKind::None");

    // Inject Reset event
    recording.inject_event(Event{.kind = EventKind::Reset});
    expect(selected_device().take_event(ev) == Status::Ok && ev.kind == EventKind::Reset,
           "take_event pops injected Reset");
}

void setup_handshake_contract() {
    set_device(recording);

    // Inject Setup request 1: expects reply
    SetupPacket get_desc{.request_type = 0x80, .request = 0x06, .length = 18};
    recording.inject_event(Event{.kind = EventKind::Setup, .setup = get_desc});

    Event ev;
    expect(selected_device().take_event(ev) == Status::Ok, "take_event gets Setup");
    expect(ev.kind == EventKind::Setup, "event kind is Setup");
    expect(ev.setup.length == 18, "setup length matches");

    std::array<std::byte, 18> desc_reply{std::byte{18}, std::byte{1}};
    expect(selected_device().control_reply(desc_reply) == Status::Ok,
           "first control_reply succeeds");
    expect(recording.reply_count_ == 1, "reply count incremented");

    // Exactly one reply or stall: calling reply or stall again fails
    expect(selected_device().control_reply(desc_reply) == Status::BadArgument,
           "second control_reply fails when no Setup is pending");
    expect(selected_device().control_stall() == Status::BadArgument,
           "control_stall fails when no Setup is pending");

    // Inject Setup request 2: expects stall
    SetupPacket bad_req{.request_type = 0x00, .request = 0xff};
    recording.inject_event(Event{.kind = EventKind::Setup, .setup = bad_req});
    expect(selected_device().take_event(ev) == Status::Ok && ev.kind == EventKind::Setup,
           "take_event gets second Setup");

    expect(selected_device().control_stall() == Status::Ok, "first control_stall succeeds");
    expect(recording.stall_count_ == 1, "stall count incremented");
    expect(selected_device().control_stall() == Status::BadArgument,
           "second control_stall fails");
    expect(selected_device().control_reply(desc_reply) == Status::BadArgument,
           "control_reply fails after stall");

    // Setup request with OUT data stage
    SetupPacket out_req{.request_type = 0x21, .request = 0x20, .length = 7};
    recording.inject_event(Event{.kind = EventKind::Setup, .setup = out_req});
    const std::array out_data{std::byte{0x00}, std::byte{0x96}, std::byte{0x00}, std::byte{0x00}};
    recording.inject_control_data(out_data);

    expect(selected_device().take_event(ev) == Status::Ok && ev.kind == EventKind::Setup,
           "take_event gets OUT Setup");
    std::array<std::byte, 16> rx_buf{};
    std::size_t received = 0;
    expect(selected_device().control_receive(rx_buf, received) == Status::Ok,
           "control_receive takes OUT data");
    expect(received == out_data.size(), "received count matches injected data");
    expect(rx_buf[1] == std::byte{0x96}, "received data byte matches");
    expect(selected_device().control_reply({}) == Status::Ok,
           "control_reply completes status stage");
}

void endpoint_transfer_semantics() {
    set_device(recording);
    recording.set_state(State::Configured);

    // Empty IN endpoint read answers Ok with zero
    std::array<std::byte, 32> buffer{};
    std::size_t received = 99;
    expect(selected_device().read(EndpointAddress{0x81}, buffer, received) == Status::Ok,
           "empty endpoint read is Ok");
    expect(received == 0, "empty endpoint returns received count 0");

    // Queued data IN endpoint read
    const std::array packet{std::byte{1}, std::byte{2}, std::byte{3}};
    recording.queue_read_data(EndpointAddress{0x81}, packet);
    expect(selected_device().read(EndpointAddress{0x81}, buffer, received) == Status::Ok,
           "non-empty endpoint read is Ok");
    expect(received == 3, "received 3 bytes");
    expect(buffer[0] == std::byte{1} && buffer[2] == std::byte{3}, "data bytes match");

    // OUT endpoint write into available buffer
    std::size_t accepted = 99;
    expect(selected_device().write(EndpointAddress{0x02}, packet, accepted) == Status::Ok,
           "write with available capacity is Ok");
    expect(accepted == 3, "accepted 3 bytes");

    // Full OUT endpoint write answers Ok with zero
    recording.fill_write_buffer(EndpointAddress{0x03});
    expect(selected_device().write(EndpointAddress{0x03}, packet, accepted) == Status::Ok,
           "full endpoint write is Ok");
    expect(accepted == 0, "full endpoint accepted count is 0");

    // Failure leaves counts unchanged
    received = 77;
    // Reading with OUT endpoint address is BadArgument
    expect(selected_device().read(EndpointAddress{0x01}, buffer, received) == Status::BadArgument,
           "read with OUT endpoint is BadArgument");
    expect(received == 77, "failed read leaves received count unchanged");

    accepted = 88;
    // Writing with IN endpoint address is BadArgument
    expect(selected_device().write(EndpointAddress{0x82}, packet, accepted) == Status::BadArgument,
           "write with IN endpoint is BadArgument");
    expect(accepted == 88, "failed write leaves accepted count unchanged");
}

const mm::test::case_ cases[] = {
    {"fallback answers Unsupported", &fallback_answers_unsupported},
    {"recording device lifecycle and events", &recording_device_lifecycle_and_events},
    {"setup handshake contract", &setup_handshake_contract},
    {"endpoint transfer semantics", &endpoint_transfer_semantics},
};

const mm::test::registrar reg{"mm.usb.device", cases};

}  // namespace
