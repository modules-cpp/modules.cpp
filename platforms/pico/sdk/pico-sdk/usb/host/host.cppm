// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "usb-host-cxx.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module platform.pico.usb.host;

import mm.usb;
import mm.usb.host;

namespace {

using mm::usb::Direction;
using mm::usb::EndpointAddress;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::host::DeviceInfo;
using mm::usb::host::Event;
using mm::usb::host::EventKind;
using mm::usb::host::Handle;
using mm::usb::host::Host;

Status from(int code) {
    switch (code) {
        case MM_PICO_USB_HOST_OK: return Status::Ok;
        case MM_PICO_USB_HOST_BAD_ARGUMENT: return Status::BadArgument;
        case MM_PICO_USB_HOST_UNSUPPORTED: return Status::Unsupported;
        case MM_PICO_USB_HOST_NOT_INITIALIZED: return Status::NotInitialized;
        case MM_PICO_USB_HOST_BUSY: return Status::Busy;
        case MM_PICO_USB_HOST_TIMEOUT: return Status::Timeout;
        case MM_PICO_USB_HOST_TRANSPORT_ERROR: return Status::TransportError;
        default: return Status::TransportError;
    }
}

Speed to_speed(int speed) {
    switch (speed) {
        case MM_PICO_USB_HOST_SPEED_LOW: return Speed::Low;
        case MM_PICO_USB_HOST_SPEED_FULL: return Speed::Full;
        case MM_PICO_USB_HOST_SPEED_HIGH: return Speed::High;
        default: return Speed::Unknown;
    }
}

class PicoHost final : public Host {
public:
    [[nodiscard]] Status initialize() override {
        return from(mm_pico_usb_host_initialize());
    }

    [[nodiscard]] Status list(std::span<DeviceInfo> out, std::size_t& count) override {
        std::vector<mm_pico_usb_host_device_info> raw(out.size());
        size_t actual = 0;
        int ret = mm_pico_usb_host_list(raw.data(), raw.size(), &actual);
        if (ret != MM_PICO_USB_HOST_OK) return from(ret);
        count = actual;
        for (size_t i = 0; i < actual && i < out.size(); ++i) {
            out[i] = DeviceInfo{
                .bus = raw[i].bus,
                .address = raw[i].address,
                .vendor = raw[i].vendor,
                .product = raw[i].product,
                .speed = to_speed(raw[i].speed)
            };
        }
        return Status::Ok;
    }

    [[nodiscard]] Status take_event(Event& ev) override {
        mm_pico_usb_host_event raw{};
        int ret = mm_pico_usb_host_take_event(&raw);
        if (ret != MM_PICO_USB_HOST_OK) return from(ret);
        if (raw.kind == MM_PICO_USB_HOST_EVENT_ATTACHED) {
            ev.kind = EventKind::Attached;
            ev.device = DeviceInfo{
                .bus = raw.device.bus,
                .address = raw.device.address,
                .vendor = raw.device.vendor,
                .product = raw.device.product,
                .speed = to_speed(raw.device.speed)
            };
        } else if (raw.kind == MM_PICO_USB_HOST_EVENT_DETACHED) {
            ev.kind = EventKind::Detached;
            ev.device = DeviceInfo{
                .bus = raw.device.bus,
                .address = raw.device.address,
                .vendor = raw.device.vendor,
                .product = raw.device.product,
                .speed = to_speed(raw.device.speed)
            };
        } else {
            ev.kind = EventKind::None;
        }
        return Status::Ok;
    }

    [[nodiscard]] Status open(const DeviceInfo& dev, Handle& h) override {
        mm_pico_usb_host_device_info raw{
            .bus = dev.bus,
            .address = dev.address,
            .vendor = dev.vendor,
            .product = dev.product,
            .speed = 0
        };
        unsigned int handle = 0;
        int ret = mm_pico_usb_host_open(&raw, &handle);
        if (ret == MM_PICO_USB_HOST_OK) {
            h = Handle{.value = handle};
        }
        return from(ret);
    }

    [[nodiscard]] Status close(Handle h) override {
        return from(mm_pico_usb_host_close(h.value));
    }

    [[nodiscard]] Status descriptors(Handle h, std::span<std::byte> out, std::size_t& transferred) override {
        size_t actual = 0;
        int ret = mm_pico_usb_host_descriptors(h.value, reinterpret_cast<uint8_t*>(out.data()), out.size(), &actual);
        if (ret == MM_PICO_USB_HOST_OK) {
            transferred = actual;
        }
        return from(ret);
    }

    [[nodiscard]] Status claim(Handle h, unsigned int iface) override {
        return from(mm_pico_usb_host_claim(h.value, iface));
    }

    [[nodiscard]] Status release(Handle h, unsigned int iface) override {
        return from(mm_pico_usb_host_release(h.value, iface));
    }

    [[nodiscard]] Status control(Handle h, const SetupPacket& setup,
                                 std::span<std::byte> buf,
                                 std::size_t& transferred,
                                 unsigned long timeout_ms) override {
        mm_pico_usb_host_setup raw{
            .request_type = setup.request_type,
            .request = setup.request,
            .value = setup.value,
            .index = setup.index,
            .length = setup.length
        };
        size_t actual = 0;
        int ret = mm_pico_usb_host_control(
            h.value, &raw, reinterpret_cast<uint8_t*>(buf.data()), buf.size(), &actual, timeout_ms);
        if (ret == MM_PICO_USB_HOST_OK) {
            transferred = actual;
        }
        return from(ret);
    }

    [[nodiscard]] Status transfer_out(Handle h, EndpointAddress ep,
                                      std::span<const std::byte> buf,
                                      std::size_t& transferred,
                                      unsigned long timeout_ms) override {
        size_t actual = 0;
        int ret = mm_pico_usb_host_transfer(
            h.value, ep.value, const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buf.data())),
            buf.size(), &actual, timeout_ms);
        if (ret == MM_PICO_USB_HOST_OK) {
            transferred = actual;
        }
        return from(ret);
    }

    [[nodiscard]] Status transfer_in(Handle h, EndpointAddress ep,
                                     std::span<std::byte> buf,
                                     std::size_t& transferred,
                                     unsigned long timeout_ms) override {
        size_t actual = 0;
        int ret = mm_pico_usb_host_transfer(
            h.value, ep.value, reinterpret_cast<uint8_t*>(buf.data()),
            buf.size(), &actual, timeout_ms);
        if (ret == MM_PICO_USB_HOST_OK) {
            transferred = actual;
        }
        return from(ret);
    }
};

PicoHost pico_host;

struct Register {
    Register() { mm::usb::host::set_host(pico_host); }
};

const Register registered;

}  // namespace
