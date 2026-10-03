// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "usb-device-cxx.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module platform.pico.usb.device;

import mm.usb;
import mm.usb.device;

namespace {

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

Status from(int code) {
    switch (code) {
        case MM_PICO_USB_OK: return Status::Ok;
        case MM_PICO_USB_BAD_ARGUMENT: return Status::BadArgument;
        case MM_PICO_USB_UNSUPPORTED: return Status::Unsupported;
        case MM_PICO_USB_NOT_INITIALIZED: return Status::NotInitialized;
        case MM_PICO_USB_BUSY: return Status::Busy;
        case MM_PICO_USB_TIMEOUT: return Status::Timeout;
        case MM_PICO_USB_TRANSPORT_ERROR: return Status::TransportError;
        default: return Status::TransportError;
    }
}

State to_state(int state) {
    switch (state) {
        case MM_PICO_USB_STATE_DETACHED: return State::Detached;
        case MM_PICO_USB_STATE_ATTACHED: return State::Attached;
        case MM_PICO_USB_STATE_POWERED: return State::Powered;
        case MM_PICO_USB_STATE_DEFAULT: return State::Default;
        case MM_PICO_USB_STATE_ADDRESS: return State::Address;
        case MM_PICO_USB_STATE_CONFIGURED: return State::Configured;
        case MM_PICO_USB_STATE_SUSPENDED: return State::Suspended;
        default: return State::Detached;
    }
}

Speed to_speed(int speed) {
    switch (speed) {
        case MM_PICO_USB_SPEED_LOW: return Speed::Low;
        case MM_PICO_USB_SPEED_FULL: return Speed::Full;
        case MM_PICO_USB_SPEED_HIGH: return Speed::High;
        default: return Speed::Unknown;
    }
}

class PicoDevice final : public Device {
public:
    [[nodiscard]] Status initialize(const Descriptors& descs) override {
        std::vector<const uint8_t*> str_ptrs;
        std::vector<size_t> str_lens;
        str_ptrs.reserve(descs.strings.size());
        str_lens.reserve(descs.strings.size());
        for (const auto& s : descs.strings) {
            str_ptrs.push_back(reinterpret_cast<const uint8_t*>(s.data()));
            str_lens.push_back(s.size());
        }

        return from(mm_pico_usb_device_initialize(
            reinterpret_cast<const uint8_t*>(descs.device.data()), descs.device.size(),
            reinterpret_cast<const uint8_t*>(descs.configuration.data()), descs.configuration.size(),
            str_ptrs.data(), str_lens.data(), descs.strings.size()));
    }

    [[nodiscard]] Status attach() override {
        return from(mm_pico_usb_device_attach());
    }

    [[nodiscard]] Status detach() override {
        return from(mm_pico_usb_device_detach());
    }

    [[nodiscard]] State state() const override {
        int st = 0;
        if (mm_pico_usb_device_state(&st) != MM_PICO_USB_OK) return State::Detached;
        return to_state(st);
    }

    [[nodiscard]] Speed speed() const override {
        int sp = 0;
        if (mm_pico_usb_device_speed(&sp) != MM_PICO_USB_OK) return Speed::Unknown;
        return to_speed(sp);
    }

    [[nodiscard]] Status take_event(Event& ev) override {
        struct mm_pico_usb_event raw{};
        int ret = mm_pico_usb_device_take_event(&raw);
        if (ret != MM_PICO_USB_OK) return from(ret);

        switch (raw.kind) {
            case MM_PICO_USB_EVENT_RESET: ev.kind = EventKind::Reset; break;
            case MM_PICO_USB_EVENT_CONFIGURED: ev.kind = EventKind::Configured; break;
            case MM_PICO_USB_EVENT_DECONFIGURED: ev.kind = EventKind::Deconfigured; break;
            case MM_PICO_USB_EVENT_SUSPEND: ev.kind = EventKind::Suspend; break;
            case MM_PICO_USB_EVENT_RESUME: ev.kind = EventKind::Resume; break;
            case MM_PICO_USB_EVENT_SETUP:
                ev.kind = EventKind::Setup;
                ev.setup = SetupPacket{
                    .request_type = raw.setup.request_type,
                    .request = raw.setup.request,
                    .value = raw.setup.value,
                    .index = raw.setup.index,
                    .length = raw.setup.length
                };
                break;
            default:
                ev.kind = EventKind::None;
                break;
        }
        return Status::Ok;
    }

    [[nodiscard]] Status control_receive(std::span<std::byte> buf, std::size_t& received) override {
        size_t raw = 0;
        int ret = mm_pico_usb_device_control_receive(
            reinterpret_cast<uint8_t*>(buf.data()), buf.size(), &raw);
        if (ret == MM_PICO_USB_OK) received = raw;
        return from(ret);
    }

    [[nodiscard]] Status control_reply(std::span<const std::byte> data) override {
        return from(mm_pico_usb_device_control_reply(
            reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    }

    [[nodiscard]] Status control_stall() override {
        return from(mm_pico_usb_device_control_stall());
    }

    [[nodiscard]] Status write(EndpointAddress ep, std::span<const std::byte> buf,
                               std::size_t& transferred) override {
        size_t raw = 0;
        int ret = mm_pico_usb_device_write(
            ep.value, reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), &raw);
        if (ret == MM_PICO_USB_OK) transferred = raw;
        return from(ret);
    }

    [[nodiscard]] Status read(EndpointAddress ep, std::span<std::byte> buf,
                              std::size_t& transferred) override {
        size_t raw = 0;
        int ret = mm_pico_usb_device_read(
            ep.value, reinterpret_cast<uint8_t*>(buf.data()), buf.size(), &raw);
        if (ret == MM_PICO_USB_OK) transferred = raw;
        return from(ret);
    }

    [[nodiscard]] Status stall(EndpointAddress ep, bool enable) override {
        return from(mm_pico_usb_device_stall(ep.value, enable ? 1 : 0));
    }
};

PicoDevice pico_device;

struct Register {
    Register() { mm::usb::device::set_device(pico_device); }
};

const Register registered;

}  // namespace
