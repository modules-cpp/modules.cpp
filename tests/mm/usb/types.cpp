// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdint>

import mm.usb;
import mm.test;

namespace {

using mm::test::expect;
using mm::usb::Direction;
using mm::usb::EndpointAddress;
using mm::usb::Recipient;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::TransferType;
using mm::usb::Type;

void endpoint_address_decoding() {
    constexpr EndpointAddress out_zero{0x00};
    expect(out_zero.number() == 0, "endpoint 0 number is 0");
    expect(!out_zero.in(), "endpoint 0x00 is OUT");
    expect(out_zero.valid(), "endpoint 0x00 is valid");

    constexpr EndpointAddress in_one{0x81};
    expect(in_one.number() == 1, "endpoint 0x81 number is 1");
    expect(in_one.in(), "endpoint 0x81 is IN");
    expect(in_one.valid(), "endpoint 0x81 is valid");

    constexpr EndpointAddress in_fifteen{0x8f};
    expect(in_fifteen.number() == 15, "endpoint 0x8f number is 15");
    expect(in_fifteen.in(), "endpoint 0x8f is IN");
    expect(in_fifteen.valid(), "endpoint 0x8f is valid");

    constexpr EndpointAddress out_fifteen{0x0f};
    expect(out_fifteen.number() == 15, "endpoint 0x0f number is 15");
    expect(!out_fifteen.in(), "endpoint 0x0f is OUT");
    expect(out_fifteen.valid(), "endpoint 0x0f is valid");

    // Bits 4 to 6 reserved (must be zero)
    expect(!EndpointAddress{0x10}.valid(), "bit 4 set is invalid");
    expect(!EndpointAddress{0x20}.valid(), "bit 5 set is invalid");
    expect(!EndpointAddress{0x40}.valid(), "bit 6 set is invalid");
    expect(!EndpointAddress{0x70}.valid(), "bits 4-6 set is invalid");
    expect(!EndpointAddress{0x91}.valid(), "bit 4 set on IN endpoint is invalid");

    expect(EndpointAddress{0x81} == EndpointAddress{0x81}, "endpoint address equality");
    expect(!(EndpointAddress{0x81} == EndpointAddress{0x01}), "endpoint address inequality on direction");
}

void setup_packet_decoding() {
    // Standard Device GET_STATUS (0x80)
    SetupPacket get_status{
        .request_type = 0x80,
        .request = 0x00,
        .value = 0,
        .index = 0,
        .length = 2
    };
    expect(get_status.direction() == Direction::In, "0x80 direction is IN");
    expect(get_status.type() == Type::Standard, "0x80 type is Standard");
    expect(get_status.recipient() == Recipient::Device, "0x80 recipient is Device");

    // Class Interface SET_LINE_CODING (0x21)
    SetupPacket set_line_coding{
        .request_type = 0x21,
        .request = 0x20,
        .value = 0,
        .index = 0,
        .length = 7
    };
    expect(set_line_coding.direction() == Direction::Out, "0x21 direction is OUT");
    expect(set_line_coding.type() == Type::Class, "0x21 type is Class");
    expect(set_line_coding.recipient() == Recipient::Interface, "0x21 recipient is Interface");

    // Vendor Device Request (0x40 OUT, 0xc0 IN)
    SetupPacket vendor_out{.request_type = 0x40};
    expect(vendor_out.direction() == Direction::Out, "0x40 direction is OUT");
    expect(vendor_out.type() == Type::Vendor, "0x40 type is Vendor");
    expect(vendor_out.recipient() == Recipient::Device, "0x40 recipient is Device");

    SetupPacket vendor_in{.request_type = 0xc0};
    expect(vendor_in.direction() == Direction::In, "0xc0 direction is IN");
    expect(vendor_in.type() == Type::Vendor, "0xc0 type is Vendor");
    expect(vendor_in.recipient() == Recipient::Device, "0xc0 recipient is Device");

    // Endpoint Recipient (0x02 OUT, 0x82 IN)
    SetupPacket ep_clear_feature{.request_type = 0x02};
    expect(ep_clear_feature.recipient() == Recipient::Endpoint, "0x02 recipient is Endpoint");

    // Other Recipient (0x03) and Reserved Type (0x60)
    SetupPacket other_reserved{.request_type = 0x63};
    expect(other_reserved.type() == Type::Reserved, "0x63 type is Reserved");
    expect(other_reserved.recipient() == Recipient::Other, "0x63 recipient is Other");

    // Out of range recipient (> 3) maps to Other
    SetupPacket custom_recip{.request_type = 0x1f};
    expect(custom_recip.recipient() == Recipient::Other, "0x1f recipient maps to Other");

    expect(get_status == get_status, "SetupPacket equality");
    expect(!(get_status == set_line_coding), "SetupPacket inequality");
}

void enumerations_and_vocabulary() {
    expect(Speed::Unknown != Speed::Full, "Speed enum distinctions");
    expect(TransferType::Control != TransferType::Bulk, "TransferType distinctions");
    expect(Status::Ok != Status::Unsupported, "Status distinctions");
}

const mm::test::case_ cases[] = {
    {"endpoint address decoding", &endpoint_address_decoding},
    {"setup packet decoding", &setup_packet_decoding},
    {"enumerations and vocabulary", &enumerations_and_vocabulary},
};

const mm::test::registrar reg{"mm.usb types", cases};

}  // namespace
