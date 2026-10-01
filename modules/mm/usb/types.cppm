// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>

export module mm.usb:types;

export namespace mm::usb {

enum class Direction { Out, In };
enum class Type { Standard, Class, Vendor, Reserved };
enum class Recipient { Device, Interface, Endpoint, Other };

// Only what integrated PHYs and the Linux stacks report. SuperSpeed is added
// when a provider can produce it.
enum class Speed { Unknown, Low, Full, High };

enum class TransferType { Control, Isochronous, Bulk, Interrupt };

// bEndpointAddress as descriptors carry it: bit 7 the direction (1 is IN,
// device to host), bits 0 to 3 the number, 0 to 15.
struct EndpointAddress {
    std::uint8_t value = 0;
    [[nodiscard]] constexpr unsigned int number() const { return value & 0x0f; }
    [[nodiscard]] constexpr bool in() const { return (value & 0x80) != 0; }
    [[nodiscard]] constexpr bool valid() const { return (value & 0x70) == 0; }
    constexpr bool operator==(const EndpointAddress&) const = default;
};

// The eight bytes of a control request, in host order.
struct SetupPacket {
    using Direction = mm::usb::Direction;
    using Type = mm::usb::Type;
    using Recipient = mm::usb::Recipient;

    std::uint8_t request_type = 0;   // bmRequestType
    std::uint8_t request = 0;        // bRequest
    std::uint16_t value = 0;         // wValue
    std::uint16_t index = 0;         // wIndex
    std::uint16_t length = 0;        // wLength

    [[nodiscard]] constexpr Direction direction() const {
        return (request_type & 0x80) ? Direction::In : Direction::Out;
    }

    [[nodiscard]] constexpr Type type() const {
        switch ((request_type >> 5) & 0x03) {
        case 0: return Type::Standard;
        case 1: return Type::Class;
        case 2: return Type::Vendor;
        default: return Type::Reserved;
        }
    }

    [[nodiscard]] constexpr Recipient recipient() const {
        switch (request_type & 0x1f) {
        case 0: return Recipient::Device;
        case 1: return Recipient::Interface;
        case 2: return Recipient::Endpoint;
        case 3: return Recipient::Other;
        default: return Recipient::Other;
        }
    }

    constexpr bool operator==(const SetupPacket&) const = default;
};

}
