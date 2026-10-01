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

export module mm.usb.cdc;

import mm.usb;
import mm.usb.device;

export namespace mm::usb::cdc {

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

constexpr uint16_t VENDOR_ID = 0x1d50;
constexpr uint16_t PRODUCT_ID = 0x6151;

constexpr uint8_t CDC_REQUEST_SET_LINE_CODING = 0x20;
constexpr uint8_t CDC_REQUEST_GET_LINE_CODING = 0x21;
constexpr uint8_t CDC_REQUEST_SET_CONTROL_LINE_STATE = 0x22;
constexpr uint8_t CDC_REQUEST_SEND_BREAK = 0x23;

constexpr EndpointAddress ENDPOINT_BULK_OUT{0x01};
constexpr EndpointAddress ENDPOINT_BULK_IN{0x81};
constexpr EndpointAddress ENDPOINT_NOTIFY_IN{0x82};

struct LineCoding {
    uint32_t bitrate = 115200;
    uint8_t stopbits = 0;   // 0: 1 stop bit, 1: 1.5 stop bits, 2: 2 stop bits
    uint8_t parity = 0;     // 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
    uint8_t databits = 8;   // 5, 6, 7, 8, 16

    constexpr bool operator==(const LineCoding&) const = default;
};

inline std::array<std::byte, 7> encode_line_coding(const LineCoding& lc) {
    return {
        static_cast<std::byte>(lc.bitrate & 0xff),
        static_cast<std::byte>((lc.bitrate >> 8) & 0xff),
        static_cast<std::byte>((lc.bitrate >> 16) & 0xff),
        static_cast<std::byte>((lc.bitrate >> 24) & 0xff),
        static_cast<std::byte>(lc.stopbits),
        static_cast<std::byte>(lc.parity),
        static_cast<std::byte>(lc.databits)
    };
}

inline bool decode_line_coding(std::span<const std::byte> bytes, LineCoding& lc) {
    if (bytes.size() < 7) return false;
    lc.bitrate = static_cast<uint32_t>(bytes[0]) |
                 (static_cast<uint32_t>(bytes[1]) << 8) |
                 (static_cast<uint32_t>(bytes[2]) << 16) |
                 (static_cast<uint32_t>(bytes[3]) << 24);
    lc.stopbits = static_cast<uint8_t>(bytes[4]);
    lc.parity = static_cast<uint8_t>(bytes[5]);
    lc.databits = static_cast<uint8_t>(bytes[6]);
    return true;
}

inline const std::vector<std::byte>& default_device_descriptor() {
    static const std::vector<std::byte> desc = {
        std::byte{18},                // bLength
        std::byte{1},                 // bDescriptorType (DEVICE)
        std::byte{0x00}, std::byte{0x02}, // bcdUSB 2.00
        std::byte{0xef},              // bDeviceClass (Miscellaneous Device)
        std::byte{0x02},              // bDeviceSubClass (Common Class)
        std::byte{0x01},              // bDeviceProtocol (Interface Association Descriptor)
        std::byte{64},                // bMaxPacketSize0
        std::byte{0x50}, std::byte{0x1d}, // idVendor 0x1d50
        std::byte{0x51}, std::byte{0x61}, // idProduct 0x6151
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
        std::byte{75}, std::byte{0},  // wTotalLength (75 bytes)
        std::byte{2},                 // bNumInterfaces
        std::byte{1},                 // bConfigurationValue
        std::byte{0},                 // iConfiguration
        std::byte{0x80},              // bmAttributes (bus powered)
        std::byte{50},                // bMaxPower (100 mA)

        // Interface Association Descriptor (8 bytes)
        std::byte{8},                 // bLength
        std::byte{11},                // bDescriptorType (INTERFACE ASSOCIATION)
        std::byte{0},                 // bFirstInterface
        std::byte{2},                 // bInterfaceCount
        std::byte{0x02},              // bFunctionClass (CDC Control)
        std::byte{0x02},              // bFunctionSubClass (Abstract Control Model)
        std::byte{0x01},              // bFunctionProtocol (AT Commands / V.250)
        std::byte{0},                 // iFunction

        // Interface 0: CDC Control (9 bytes)
        std::byte{9},                 // bLength
        std::byte{4},                 // bDescriptorType (INTERFACE)
        std::byte{0},                 // bInterfaceNumber
        std::byte{0},                 // bAlternateSetting
        std::byte{1},                 // bNumEndpoints
        std::byte{0x02},              // bInterfaceClass (CDC Control)
        std::byte{0x02},              // bInterfaceSubClass (Abstract Control Model)
        std::byte{0x01},              // bInterfaceProtocol (AT Commands)
        std::byte{0},                 // iInterface

        // CDC Header Functional Descriptor (5 bytes)
        std::byte{5},                 // bLength
        std::byte{0x24},              // bDescriptorType (CS_INTERFACE)
        std::byte{0x00},              // bDescriptorSubtype (Header)
        std::byte{0x10}, std::byte{0x01}, // bcdCDC 1.10

        // CDC Call Management Functional Descriptor (5 bytes)
        std::byte{5},                 // bLength
        std::byte{0x24},              // bDescriptorType (CS_INTERFACE)
        std::byte{0x01},              // bDescriptorSubtype (Call Management)
        std::byte{0x00},              // bmCapabilities (none)
        std::byte{1},                 // bDataInterface (Interface 1)

        // CDC Abstract Control Management Functional Descriptor (4 bytes)
        std::byte{4},                 // bLength
        std::byte{0x24},              // bDescriptorType (CS_INTERFACE)
        std::byte{0x02},              // bDescriptorSubtype (ACM)
        std::byte{0x02},              // bmCapabilities (Set_Line_Coding, Get_Line_Coding, Set_Control_Line_State)

        // CDC Union Functional Descriptor (5 bytes)
        std::byte{5},                 // bLength
        std::byte{0x24},              // bDescriptorType (CS_INTERFACE)
        std::byte{0x06},              // bDescriptorSubtype (Union)
        std::byte{0},                 // bControlInterface (Interface 0)
        std::byte{1},                 // bSubordinateInterface0 (Interface 1)

        // Endpoint 2 IN: CDC Notification (7 bytes)
        std::byte{7},                 // bLength
        std::byte{5},                 // bDescriptorType (ENDPOINT)
        std::byte{0x82},              // bEndpointAddress (EP2 IN)
        std::byte{0x03},              // bmAttributes (Interrupt)
        std::byte{16}, std::byte{0},  // wMaxPacketSize (16 bytes)
        std::byte{16},                // bInterval (16 ms)

        // Interface 1: CDC Data (9 bytes)
        std::byte{9},                 // bLength
        std::byte{4},                 // bDescriptorType (INTERFACE)
        std::byte{1},                 // bInterfaceNumber
        std::byte{0},                 // bAlternateSetting
        std::byte{2},                 // bNumEndpoints
        std::byte{0x0a},              // bInterfaceClass (CDC Data)
        std::byte{0x00},              // bInterfaceSubClass
        std::byte{0x00},              // bInterfaceProtocol
        std::byte{0},                 // iInterface

        // Endpoint 1 OUT: Bulk Data (7 bytes)
        std::byte{7},                 // bLength
        std::byte{5},                 // bDescriptorType (ENDPOINT)
        std::byte{0x01},              // bEndpointAddress (EP1 OUT)
        std::byte{0x02},              // bmAttributes (Bulk)
        std::byte{64}, std::byte{0},  // wMaxPacketSize (64 bytes for Full Speed)
        std::byte{0},                 // bInterval

        // Endpoint 1 IN: Bulk Data (7 bytes)
        std::byte{7},                 // bLength
        std::byte{5},                 // bDescriptorType (ENDPOINT)
        std::byte{0x81},              // bEndpointAddress (EP1 IN)
        std::byte{0x02},              // bmAttributes (Bulk)
        std::byte{64}, std::byte{0},  // wMaxPacketSize (64 bytes for Full Speed)
        std::byte{0}                  // bInterval
    };
    return desc;
}

inline const std::vector<std::byte>& string_langid() {
    static const std::vector<std::byte> s = {std::byte{4}, std::byte{3}, std::byte{0x09}, std::byte{0x04}};
    return s;
}

inline const std::vector<std::byte>& string_manufacturer() {
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
    static const std::vector<std::byte> s = {
        std::byte{24}, std::byte{3},
        std::byte{'U'}, std::byte{0},
        std::byte{'S'}, std::byte{0},
        std::byte{'B'}, std::byte{0},
        std::byte{' '}, std::byte{0},
        std::byte{'C'}, std::byte{0},
        std::byte{'D'}, std::byte{0},
        std::byte{'C'}, std::byte{0},
        std::byte{' '}, std::byte{0},
        std::byte{'A'}, std::byte{0},
        std::byte{'C'}, std::byte{0},
        std::byte{'M'}, std::byte{0}
    };
    return s;
}

inline const std::vector<std::byte>& string_serial() {
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

class CdcDevice {
public:
    explicit CdcDevice(Device& device) : device_(device) {}

    [[nodiscard]] Status initialize() {
        return device_.initialize(default_descriptors());
    }

    [[nodiscard]] Status attach() {
        return device_.attach();
    }

    [[nodiscard]] Status detach() {
        return device_.detach();
    }

    [[nodiscard]] State state() const {
        return device_.state();
    }

    [[nodiscard]] Speed speed() const {
        return device_.speed();
    }

    [[nodiscard]] const LineCoding& line_coding() const noexcept {
        return line_coding_;
    }

    [[nodiscard]] bool dtr() const noexcept {
        return dtr_;
    }

    [[nodiscard]] bool rts() const noexcept {
        return rts_;
    }

    [[nodiscard]] bool connected() const noexcept {
        return device_.state() == State::Configured && dtr_;
    }

    [[nodiscard]] Status poll() {
        Event ev;
        const Status ev_status = device_.take_event(ev);
        if (ev_status == Status::Ok) {
            if (ev.kind == EventKind::Setup) {
                if (ev.setup.type() == Type::Class) {
                    switch (ev.setup.request) {
                        case CDC_REQUEST_SET_LINE_CODING: {
                            std::array<std::byte, 7> buf{};
                            std::size_t received = 0;
                            const Status s_rx = device_.control_receive(buf, received);
                            if (s_rx == Status::Ok && received == 7) {
                                (void)decode_line_coding(buf, line_coding_);
                                (void)device_.control_reply({});
                            } else {
                                (void)device_.control_stall();
                            }
                            break;
                        }
                        case CDC_REQUEST_GET_LINE_CODING: {
                            const auto enc = encode_line_coding(line_coding_);
                            const std::size_t len = std::min(
                                static_cast<std::size_t>(ev.setup.length), enc.size());
                            (void)device_.control_reply(std::span{enc.data(), len});
                            break;
                        }
                        case CDC_REQUEST_SET_CONTROL_LINE_STATE: {
                            dtr_ = (ev.setup.value & 0x01) != 0;
                            rts_ = (ev.setup.value & 0x02) != 0;
                            (void)device_.control_reply({});
                            break;
                        }
                        case CDC_REQUEST_SEND_BREAK: {
                            (void)device_.control_reply({});
                            break;
                        }
                        default:
                            (void)device_.control_stall();
                            break;
                    }
                } else {
                    (void)device_.control_stall();
                }
            } else if (ev.kind == EventKind::Reset || ev.kind == EventKind::Deconfigured) {
                dtr_ = false;
                rts_ = false;
            }
        }
        return Status::Ok;
    }

    [[nodiscard]] Status write(std::span<const std::byte> data, std::size_t& written) {
        if (device_.state() != State::Configured) {
            written = 0;
            return Status::Ok;
        }
        return device_.write(ENDPOINT_BULK_IN, data, written);
    }

    [[nodiscard]] Status read(std::span<std::byte> buf, std::size_t& received) {
        if (device_.state() != State::Configured) {
            received = 0;
            return Status::Ok;
        }
        return device_.read(ENDPOINT_BULK_OUT, buf, received);
    }

private:
    Device& device_;
    LineCoding line_coding_{.bitrate = 115200, .stopbits = 0, .parity = 0, .databits = 8};
    bool dtr_ = false;
    bool rts_ = false;
};

}  // namespace mm::usb::cdc
