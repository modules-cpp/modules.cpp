// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <span>

import mm.usb;
import mm.usb.device;
import mm.test;
import platform.linux.map;
import platform.linux.defaults;
import platform.linux.usb.device;

namespace {

using mm::test::expect;
using mm::usb::Direction;
using mm::usb::EndpointAddress;
using mm::usb::Recipient;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::Type;
using mm::usb::device::Descriptors;
using mm::usb::device::Event;
using mm::usb::device::EventKind;
using mm::usb::device::State;
using namespace platform::linux::usb::device;

uint32_t read_le32(const std::byte* p) {
    return static_cast<uint8_t>(p[0]) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24);
}

uint16_t read_le16(const std::byte* p) {
    return static_cast<uint8_t>(p[0]) |
           (static_cast<uint16_t>(static_cast<uint8_t>(p[1])) << 8);
}

void functionfs_descriptor_block_formatting() {
    // 9-byte configuration descriptor
    // 9-byte interface descriptor
    // 7-byte endpoint descriptor 1 (0x81 IN)
    // 7-byte endpoint descriptor 2 (0x02 OUT)
    const std::vector<std::byte> cfg = {
        std::byte{9}, std::byte{2}, std::byte{32}, std::byte{0}, std::byte{1}, std::byte{1}, std::byte{0}, std::byte{0x80}, std::byte{50},
        // Interface 0
        std::byte{9}, std::byte{4}, std::byte{0}, std::byte{0}, std::byte{2}, std::byte{0xff}, std::byte{0x00}, std::byte{0x00}, std::byte{0},
        // EP 0x81 IN bulk, max packet 512
        std::byte{7}, std::byte{5}, std::byte{0x81}, std::byte{2}, std::byte{0x00}, std::byte{0x02}, std::byte{0},
        // EP 0x02 OUT bulk, max packet 512
        std::byte{7}, std::byte{5}, std::byte{0x02}, std::byte{2}, std::byte{0x00}, std::byte{0x02}, std::byte{0}
    };

    const Descriptors descs{
        .device = {},
        .configuration = cfg,
        .strings = {}
    };

    std::vector<std::byte> block;
    const Status s = build_functionfs_descriptors(descs, block);
    expect(s == Status::Ok, "build_functionfs_descriptors succeeds for valid configuration");

    const size_t sub_len = 9 + 7 + 7; // 23 bytes
    const size_t expected_total = 20 + sub_len * 2; // 66 bytes
    expect(block.size() == expected_total, "total descriptor block length is correct");

    // Header checks
    expect(read_le32(&block[0]) == FUNCTIONFS_DESCRIPTORS_MAGIC_V2, "magic is V2");
    expect(read_le32(&block[4]) == expected_total, "length field matches total size");
    expect(read_le32(&block[8]) == (FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC), "flags has FS and HS");
    expect(read_le32(&block[12]) == 3, "fs_count is 3 descriptors");
    expect(read_le32(&block[16]) == 3, "hs_count is 3 descriptors");

    // Subordinate descriptors (FS copy)
    const std::span<const std::byte> sub = std::span{cfg}.subspan(9);
    for (size_t i = 0; i < sub_len; ++i) {
        expect(block[20 + i] == sub[i], "FS descriptor content matches");
    }

    // Subordinate descriptors (HS copy)
    for (size_t i = 0; i < sub_len; ++i) {
        expect(block[20 + sub_len + i] == sub[i], "HS descriptor content matches");
    }
}

void malformed_descriptor_rejection() {
    std::vector<std::byte> block;

    // Empty configuration
    const Descriptors empty_cfg{.configuration = {}};
    expect(build_functionfs_descriptors(empty_cfg, block) == Status::BadArgument,
           "empty configuration is rejected");

    // Truncated config header
    const std::vector<std::byte> truncated_cfg = {std::byte{9}, std::byte{2}, std::byte{9}};
    const Descriptors trunc_descs{.configuration = truncated_cfg};
    expect(build_functionfs_descriptors(trunc_descs, block) == Status::BadArgument,
           "truncated config header is rejected");

    // Config header claiming invalid subordinate length
    const std::vector<std::byte> invalid_sub = {
        std::byte{9}, std::byte{2}, std::byte{12}, std::byte{0}, std::byte{1}, std::byte{1}, std::byte{0}, std::byte{0x80}, std::byte{50},
        std::byte{5} // claims 5 bytes but only 1 byte remains
    };
    const Descriptors invalid_descs{.configuration = invalid_sub};
    expect(build_functionfs_descriptors(invalid_descs, block) == Status::BadArgument,
           "subordinate descriptor overflow is rejected");
}

void functionfs_strings_formatting() {
    // String 0: LANGID 0x0409
    const std::vector<std::byte> s0 = {std::byte{4}, std::byte{3}, std::byte{0x09}, std::byte{0x04}};
    // String 1: "Hi" (H = 0x0048, i = 0x0069)
    const std::vector<std::byte> s1 = {
        std::byte{6}, std::byte{3},
        std::byte{0x48}, std::byte{0x00},
        std::byte{0x69}, std::byte{0x00}
    };
    const std::span<const std::byte> strings_arr[] = {s0, s1};

    const Descriptors descs{
        .strings = strings_arr
    };

    std::vector<std::byte> block;
    const Status s = build_functionfs_strings(descs, block);
    expect(s == Status::Ok, "build_functionfs_strings succeeds");

    // Magic (4), Length (4), str_count (4), lang_count (4), lang (2), "Hi\0" (3) = 21 bytes
    expect(block.size() == 21, "strings block length is 21 bytes");
    expect(read_le32(&block[0]) == FUNCTIONFS_STRINGS_MAGIC, "magic is FUNCTIONFS_STRINGS_MAGIC");
    expect(read_le32(&block[4]) == 21, "length field is 21");
    expect(read_le32(&block[8]) == 1, "str_count is 1");
    expect(read_le32(&block[12]) == 1, "lang_count is 1");
    expect(read_le16(&block[16]) == 0x0409, "LANGID is 0x0409");
    expect(block[18] == std::byte{'H'} && block[19] == std::byte{'i'} && block[20] == std::byte{0},
           "UTF-8 string matches 'Hi\\0'");

    // Empty strings
    const Descriptors empty_descs{};
    std::vector<std::byte> empty_block;
    expect(build_functionfs_strings(empty_descs, empty_block) == Status::Ok, "empty strings succeed");
    expect(empty_block.size() == 16, "empty strings block size is 16 bytes");
    expect(read_le32(&empty_block[0]) == FUNCTIONFS_STRINGS_MAGIC, "magic is correct");
    expect(read_le32(&empty_block[8]) == 0, "str_count is 0");
    expect(read_le32(&empty_block[12]) == 0, "lang_count is 0");
}

void functionfs_event_translation_and_state() {
    State state = State::Detached;
    Event ev;

    // BIND -> moves state to Default, returns false
    usb_functionfs_event ffs_bind = {};
    ffs_bind.type = FUNCTIONFS_BIND;
    expect(!translate_functionfs_event(ffs_bind, state, ev), "BIND produces no public event");
    expect(state == State::Default, "BIND moves state to Default");

    // ENABLE -> moves state to Configured, returns Configured
    usb_functionfs_event ffs_enable = {};
    ffs_enable.type = FUNCTIONFS_ENABLE;
    expect(translate_functionfs_event(ffs_enable, state, ev), "ENABLE produces an event");
    expect(ev.kind == EventKind::Configured, "ENABLE event is Configured");
    expect(state == State::Configured, "ENABLE moves state to Configured");

    // SUSPEND -> moves state to Suspended, returns Suspended
    usb_functionfs_event ffs_suspend = {};
    ffs_suspend.type = FUNCTIONFS_SUSPEND;
    expect(translate_functionfs_event(ffs_suspend, state, ev), "SUSPEND produces an event");
    expect(ev.kind == EventKind::Suspended, "SUSPEND event is Suspended");
    expect(state == State::Suspended, "SUSPEND moves state to Suspended");

    // RESUME -> moves state to Configured, returns Resumed
    usb_functionfs_event ffs_resume = {};
    ffs_resume.type = FUNCTIONFS_RESUME;
    expect(translate_functionfs_event(ffs_resume, state, ev), "RESUME produces an event");
    expect(ev.kind == EventKind::Resumed, "RESUME event is Resumed");
    expect(state == State::Configured, "RESUME moves state to Configured");

    // DISABLE -> moves state to Default, returns Deconfigured
    usb_functionfs_event ffs_disable = {};
    ffs_disable.type = FUNCTIONFS_DISABLE;
    expect(translate_functionfs_event(ffs_disable, state, ev), "DISABLE produces an event");
    expect(ev.kind == EventKind::Deconfigured, "DISABLE event is Deconfigured");
    expect(state == State::Default, "DISABLE moves state to Default");

    // UNBIND -> moves state to Detached, returns false
    usb_functionfs_event ffs_unbind = {};
    ffs_unbind.type = FUNCTIONFS_UNBIND;
    expect(!translate_functionfs_event(ffs_unbind, state, ev), "UNBIND produces no public event");
    expect(state == State::Detached, "UNBIND moves state to Detached");

    // SETUP packet decoding
    usb_functionfs_event ffs_setup = {};
    ffs_setup.type = FUNCTIONFS_SETUP;
    ffs_setup.u.setup.bRequestType = 0x80; // IN, Standard, Device
    ffs_setup.u.setup.bRequest = 0x06;     // GET_DESCRIPTOR
    ffs_setup.u.setup.wValue = 0x0200;     // Configuration 0
    ffs_setup.u.setup.wIndex = 0x0000;
    ffs_setup.u.setup.wLength = 0x0040;

    expect(translate_functionfs_event(ffs_setup, state, ev), "SETUP produces an event");
    expect(ev.kind == EventKind::Setup, "SETUP event kind is Setup");
    expect(ev.setup.request_type == 0x80, "bmRequestType preserved");
    expect(ev.setup.direction() == Direction::In, "SETUP direction is In");
    expect(ev.setup.type() == Type::Standard, "SETUP type is Standard");
    expect(ev.setup.recipient() == Recipient::Device, "SETUP recipient is Device");
    expect(ev.setup.request == 0x06, "bRequest is GET_DESCRIPTOR");
    expect(ev.setup.value == 0x0200, "wValue is 0x0200");
    expect(ev.setup.index == 0x0000, "wIndex is 0x0000");
    expect(ev.setup.length == 0x0040, "wLength is 0x0040");
}

void configfs_id_parsing() {
    uint16_t id = 0;
    expect(parse_configfs_id("0x1d50", id) && id == 0x1d50, "0x1d50 parses");
    expect(parse_configfs_id("0X6150", id) && id == 0x6150, "0X6150 parses");
    expect(parse_configfs_id("1234", id) && id == 1234, "decimal 1234 parses");
    expect(parse_configfs_id("  0x0409\n", id) && id == 0x0409, "whitespace trimmed");
    expect(!parse_configfs_id("", id), "empty string rejected");
    expect(!parse_configfs_id("0x", id), "0x alone rejected");
    expect(!parse_configfs_id("0x10000", id), "overflow rejected");
    expect(!parse_configfs_id("xyz", id), "invalid characters rejected");
}

void gadget_vendor_product_mismatch_check() {
    const std::filesystem::path fixture_dir = "test_gadget_fixture";
    std::filesystem::remove_all(fixture_dir);
    std::filesystem::create_directories(fixture_dir);

    {
        std::ofstream vf(fixture_dir / "idVendor");
        vf << "0x1d50\n";
        std::ofstream pf(fixture_dir / "idProduct");
        pf << "0x6150\n";
    }

    DeviceTestHooks hooks;
    hooks.functionfs = "/dev/ffs-nonexistent";
    hooks.gadget = fixture_dir.string();
    set_device_test_hooks(&hooks);

    // Device descriptor with mismatched vendor (0x1234 instead of 0x1d50)
    const std::vector<std::byte> dev_desc_mismatch = {
        std::byte{18}, std::byte{1}, std::byte{0x00}, std::byte{0x02},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{64},
        std::byte{0x34}, std::byte{0x12}, // idVendor = 0x1234
        std::byte{0x50}, std::byte{0x61}, // idProduct = 0x6150
        std::byte{0x00}, std::byte{0x01}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{1}
    };

    // Device descriptor with matching vendor and product (0x1d50, 0x6150)
    const std::vector<std::byte> dev_desc_match = {
        std::byte{18}, std::byte{1}, std::byte{0x00}, std::byte{0x02},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{64},
        std::byte{0x50}, std::byte{0x1d}, // idVendor = 0x1d50
        std::byte{0x50}, std::byte{0x61}, // idProduct = 0x6150
        std::byte{0x00}, std::byte{0x01}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{1}
    };

    const std::vector<std::byte> cfg_desc = {
        std::byte{9}, std::byte{2}, std::byte{9}, std::byte{0}, std::byte{1}, std::byte{1}, std::byte{0}, std::byte{0x80}, std::byte{50}
    };

    expect(validate_gadget_ids(fixture_dir, dev_desc_mismatch) == Status::BadArgument,
           "validate_gadget_ids detects vendor mismatch");
    expect(validate_gadget_ids(fixture_dir, dev_desc_match) == Status::Ok,
           "validate_gadget_ids passes matching IDs");

    FunctionFsDevice dev;
    const Descriptors descs{
        .device = dev_desc_mismatch,
        .configuration = cfg_desc,
        .strings = {}
    };

    const Status status = dev.initialize(descs);
    expect(status == Status::BadArgument, "vendor/product mismatch against gadget configfs returns BadArgument");

    set_device_test_hooks(nullptr);
    std::filesystem::remove_all(fixture_dir);
}

void unserved_fallback_and_default_state() {
    // Map with empty usb.device.functionfs
    platform::linux::Map default_map;
    platform::linux::set_map(default_map);

    FunctionFsDevice dev;
    expect(dev.state() == State::Detached, "unconfigured state is Detached");
    expect(dev.speed() == Speed::Unknown, "unconfigured speed is Unknown");

    const Descriptors dummy{};
    expect(dev.initialize(dummy) == Status::Unsupported, "unconfigured initialize is Unsupported");
    expect(dev.attach() == Status::Unsupported, "unconfigured attach is Unsupported");
    expect(dev.detach() == Status::Unsupported, "unconfigured detach is Unsupported");

    Event ev;
    expect(dev.take_event(ev) == Status::Unsupported, "unconfigured take_event is Unsupported");

    std::size_t n = 0;
    std::byte buf[16] = {};
    expect(dev.control_receive(buf, n) == Status::Unsupported, "unconfigured control_receive is Unsupported");
    expect(dev.control_reply({}) == Status::Unsupported, "unconfigured control_reply is Unsupported");
    expect(dev.control_stall() == Status::Unsupported, "unconfigured control_stall is Unsupported");

    const EndpointAddress ep{0x81};
    expect(dev.write(ep, {}, n) == Status::Unsupported, "unconfigured write is Unsupported");
    expect(dev.read(ep, buf, n) == Status::Unsupported, "unconfigured read is Unsupported");
    expect(dev.stall(ep, true) == Status::Unsupported, "unconfigured stall is Unsupported");
}

const mm::test::case_ cases[] = {
    {"functionfs descriptor block formatting", &functionfs_descriptor_block_formatting},
    {"malformed descriptor rejection", &malformed_descriptor_rejection},
    {"functionfs strings formatting", &functionfs_strings_formatting},
    {"functionfs event translation and state", &functionfs_event_translation_and_state},
    {"configfs id parsing", &configfs_id_parsing},
    {"gadget vendor product mismatch check", &gadget_vendor_product_mismatch_check},
    {"unserved fallback and default state", &unserved_fallback_and_default_state},
};

const mm::test::registrar reg{"platform.linux.usb.device", cases};

}  // namespace
