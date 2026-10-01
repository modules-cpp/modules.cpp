// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

export module mm.usb.vendor;

import mm.usb;
import mm.usb.device;
import mm.usb.host;

export namespace mm::usb::vendor {

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

constexpr uint16_t VENDOR_ID = 0x1d50;
constexpr uint16_t PRODUCT_ID = 0x6150;
constexpr uint8_t INTERFACE_CLASS_VENDOR = 0xff;
constexpr uint8_t INTERFACE_SUBCLASS_VENDOR = 0x00;
constexpr uint8_t INTERFACE_PROTOCOL_VENDOR = 0x00;

constexpr EndpointAddress ENDPOINT_BULK_OUT{0x01};
constexpr EndpointAddress ENDPOINT_BULK_IN{0x81};

constexpr uint8_t VENDOR_REQUEST_ECHO = 0x01;
constexpr uint8_t VENDOR_REQUEST_RESET = 0x02;

// Standard Vendor Device Descriptors
inline const std::vector<std::byte>& default_device_descriptor() {
    static const std::vector<std::byte> desc = {
        std::byte{18},                // bLength
        std::byte{1},                 // bDescriptorType (DEVICE)
        std::byte{0x00}, std::byte{0x02}, // bcdUSB 2.00
        std::byte{0},                 // bDeviceClass (specified in interface)
        std::byte{0},                 // bDeviceSubClass
        std::byte{0},                 // bDeviceProtocol
        std::byte{64},                // bMaxPacketSize0
        std::byte{0x50}, std::byte{0x1d}, // idVendor 0x1d50
        std::byte{0x50}, std::byte{0x61}, // idProduct 0x6150
        std::byte{0x00}, std::byte{0x01}, // bcdDevice 1.00
        std::byte{1},                 // iManufacturer
        std::byte{2},                 // iProduct
        std::byte{3},                 // iSerialNumber
        std::byte{1}                  // bNumConfigurations
    };
    return desc;
}

inline const std::vector<std::byte>& default_configuration_descriptor() {
    static const std::vector<std::byte> desc = {
        // Configuration Descriptor (9 bytes)
        std::byte{9},                 // bLength
        std::byte{2},                 // bDescriptorType (CONFIGURATION)
        std::byte{32}, std::byte{0},  // wTotalLength (32 bytes)
        std::byte{1},                 // bNumInterfaces
        std::byte{1},                 // bConfigurationValue
        std::byte{0},                 // iConfiguration
        std::byte{0x80},              // bmAttributes (bus powered)
        std::byte{50},                // bMaxPower (100 mA)

        // Interface 0 Descriptor (9 bytes)
        std::byte{9},                 // bLength
        std::byte{4},                 // bDescriptorType (INTERFACE)
        std::byte{0},                 // bInterfaceNumber
        std::byte{0},                 // bAlternateSetting
        std::byte{2},                 // bNumEndpoints
        std::byte{INTERFACE_CLASS_VENDOR},    // bInterfaceClass (0xff)
        std::byte{INTERFACE_SUBCLASS_VENDOR}, // bInterfaceSubClass (0x00)
        std::byte{INTERFACE_PROTOCOL_VENDOR}, // bInterfaceProtocol (0x00)
        std::byte{0},                 // iInterface

        // Endpoint 1 OUT (7 bytes)
        std::byte{7},                 // bLength
        std::byte{5},                 // bDescriptorType (ENDPOINT)
        std::byte{0x01},              // bEndpointAddress (EP1 OUT)
        std::byte{0x02},              // bmAttributes (Bulk)
        std::byte{0x00}, std::byte{0x02}, // wMaxPacketSize (512 bytes)
        std::byte{0},                 // bInterval

        // Endpoint 1 IN (7 bytes)
        std::byte{7},                 // bLength
        std::byte{5},                 // bDescriptorType (ENDPOINT)
        std::byte{0x81},              // bEndpointAddress (EP1 IN)
        std::byte{0x02},              // bmAttributes (Bulk)
        std::byte{0x00}, std::byte{0x02}, // wMaxPacketSize (512 bytes)
        std::byte{0}                  // bInterval
    };
    return desc;
}

// Strings: Language list (0x0409), Manufacturer, Product, Serial
inline const std::vector<std::byte>& string_langid() {
    static const std::vector<std::byte> s = {std::byte{4}, std::byte{3}, std::byte{0x09}, std::byte{0x04}};
    return s;
}

inline const std::vector<std::byte>& string_manufacturer() {
    // "32bitmicro" in UTF-16LE
    static const std::vector<std::byte> s = {
        std::byte{22}, std::byte{3},
        std::byte{'3'}, std::byte{0},
        std::byte{'2'}, std::byte{0},
        std::byte{'b'}, std::byte{0},
        std::byte{'i'}, std::byte{0},
        std::byte{'t'}, std::byte{0},
        std::byte{'m'}, std::byte{0},
        std::byte{'i'}, std::byte{0},
        std::byte{'c'}, std::byte{0},
        std::byte{'r'}, std::byte{0},
        std::byte{'o'}, std::byte{0}
    };
    return s;
}

inline const std::vector<std::byte>& string_product() {
    // "USB Echo" in UTF-16LE
    static const std::vector<std::byte> s = {
        std::byte{18}, std::byte{3},
        std::byte{'U'}, std::byte{0},
        std::byte{'S'}, std::byte{0},
        std::byte{'B'}, std::byte{0},
        std::byte{' '}, std::byte{0},
        std::byte{'E'}, std::byte{0},
        std::byte{'c'}, std::byte{0},
        std::byte{'h'}, std::byte{0},
        std::byte{'o'}, std::byte{0}
    };
    return s;
}

inline const std::vector<std::byte>& string_serial() {
    // "0001" in UTF-16LE
    static const std::vector<std::byte> s = {
        std::byte{10}, std::byte{3},
        std::byte{'0'}, std::byte{0},
        std::byte{'0'}, std::byte{0},
        std::byte{'0'}, std::byte{0},
        std::byte{'1'}, std::byte{0}
    };
    return s;
}

inline const std::vector<std::span<const std::byte>>& default_strings_list() {
    static const std::vector<std::span<const std::byte>> list = {
        string_langid(),
        string_manufacturer(),
        string_product(),
        string_serial()
    };
    return list;
}

[[nodiscard]] inline Descriptors default_descriptors() {
    return Descriptors{
        .device = default_device_descriptor(),
        .configuration = default_configuration_descriptor(),
        .strings = default_strings_list()
    };
}

// Device-side echo service
class DeviceEcho {
public:
    explicit DeviceEcho(Device& device) : device_(device) {
        buffer_.resize(512);
    }

    [[nodiscard]] Status initialize() {
        return device_.initialize(default_descriptors());
    }

    [[nodiscard]] Status attach() {
        return device_.attach();
    }

    [[nodiscard]] Status detach() {
        return device_.detach();
    }

    [[nodiscard]] Status poll() {
        // 1. Process control/bus events
        Event ev;
        const Status ev_status = device_.take_event(ev);
        if (ev_status == Status::Ok) {
            if (ev.kind == EventKind::Setup) {
                if (ev.setup.type() == Type::Vendor) {
                    if (ev.setup.direction() == Direction::In) {
                        // Answer with small vendor status payload
                        const std::array<std::byte, 4> reply = {
                            std::byte{0xaa}, std::byte{0x55},
                            static_cast<std::byte>(total_echoed_ & 0xff),
                            static_cast<std::byte>((total_echoed_ >> 8) & 0xff)
                        };
                        const size_t len = std::min(static_cast<size_t>(ev.setup.length), reply.size());
                        (void)device_.control_reply(std::span{reply.data(), len});
                    } else {
                        (void)device_.control_reply({});
                    }
                } else {
                    (void)device_.control_stall();
                }
            } else if (ev.kind == EventKind::Reset || ev.kind == EventKind::Deconfigured) {
                pending_bytes_ = 0;
                buffer_offset_ = 0;
            }
        }

        // 2. Perform bulk echo if configured
        if (device_.state() == State::Configured) {
            // If nothing is waiting to be written back, try reading new packet
            if (pending_bytes_ == 0) {
                std::size_t received = 0;
                const Status r_stat = device_.read(ENDPOINT_BULK_OUT, buffer_, received);
                if (r_stat == Status::Ok && received > 0) {
                    pending_bytes_ = received;
                    buffer_offset_ = 0;
                }
            }

            // If we have received bytes, write them back to bulk IN
            if (pending_bytes_ > 0) {
                std::size_t accepted = 0;
                const auto out_span = std::span{buffer_.data() + buffer_offset_, pending_bytes_};
                const Status w_stat = device_.write(ENDPOINT_BULK_IN, out_span, accepted);
                if (w_stat == Status::Ok && accepted > 0) {
                    buffer_offset_ += accepted;
                    pending_bytes_ -= accepted;
                    total_echoed_ += accepted;
                }
            }
        }

        return Status::Ok;
    }

    [[nodiscard]] std::size_t total_echoed() const noexcept {
        return total_echoed_;
    }

    [[nodiscard]] State state() const {
        return device_.state();
    }

private:
    Device& device_;
    std::vector<std::byte> buffer_;
    std::size_t pending_bytes_ = 0;
    std::size_t buffer_offset_ = 0;
    std::size_t total_echoed_ = 0;
};

// Host-side echo client
class HostEcho {
public:
    explicit HostEcho(Host& host) : host_(host) {}

    [[nodiscard]] Status find_and_open(Handle& out_handle) {
        std::array<DeviceInfo, 32> devices{};
        std::size_t count = 0;
        const Status list_stat = host_.list(devices, count);
        if (list_stat != Status::Ok) return list_stat;

        for (std::size_t i = 0; i < count; ++i) {
            if (devices[i].vendor == VENDOR_ID && devices[i].product == PRODUCT_ID) {
                const Status open_stat = host_.open(devices[i], out_handle);
                if (open_stat != Status::Ok) return open_stat;
                const Status claim_stat = host_.claim(out_handle, 0);
                if (claim_stat != Status::Ok) {
                    (void)host_.close(out_handle);
                    return claim_stat;
                }
                return Status::Ok;
            }
        }
        return Status::BadArgument;
    }

    [[nodiscard]] Status echo(Handle handle, std::span<const std::byte> out_data,
                              std::span<std::byte> in_data, std::size_t& transferred,
                              unsigned long timeout_ms = 1000) {
        std::size_t sent = 0;
        const Status s_out = host_.transfer_out(handle, ENDPOINT_BULK_OUT, out_data, sent, timeout_ms);
        if (s_out != Status::Ok) return s_out;
        if (sent != out_data.size()) return Status::TransportError;

        const Status s_in = host_.transfer_in(handle, ENDPOINT_BULK_IN, in_data, transferred, timeout_ms);
        if (s_in != Status::Ok) return s_in;
        if (transferred != sent) return Status::TransportError;

        if (std::memcmp(out_data.data(), in_data.data(), transferred) != 0) {
            return Status::TransportError;
        }

        return Status::Ok;
    }

    [[nodiscard]] Status vendor_control(Handle handle, uint8_t request, uint16_t value,
                                        std::span<std::byte> data, std::size_t& transferred,
                                        unsigned long timeout_ms = 1000) {
        const SetupPacket setup{
            .request_type = 0xc0, // IN, Vendor, Device
            .request = request,
            .value = value,
            .index = 0,
            .length = static_cast<uint16_t>(data.size())
        };
        return host_.control(handle, setup, data, transferred, timeout_ms);
    }

    [[nodiscard]] Status close(Handle handle) {
        (void)host_.release(handle, 0);
        return host_.close(handle);
    }

private:
    Host& host_;
};

}  // namespace mm::usb::vendor
