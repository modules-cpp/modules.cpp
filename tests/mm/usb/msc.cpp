// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <vector>

import mm.mcu;
import mm.usb;
import mm.usb.device;
import mm.usb.msc;
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
using mm::usb::msc::CommandBlockWrapper;
using mm::usb::msc::CommandStatusWrapper;
using mm::usb::msc::StorageBackend;

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
        if (ep != mm::usb::msc::ENDPOINT_BULK_OUT) return Status::BadArgument;
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
        if (ep != mm::usb::msc::ENDPOINT_BULK_IN) return Status::BadArgument;
        bulk_in_fifo_.insert(bulk_in_fifo_.end(), buf.begin(), buf.end());
        transferred = buf.size();
        return Status::Ok;
    }

    [[nodiscard]] Status stall(EndpointAddress, bool) override {
        endpoint_stalled_ = true;
        return Status::Ok;
    }

    bool initialized_ = false;
    bool attached_ = false;
    bool replied_ = false;
    bool stalled_ = false;
    bool endpoint_stalled_ = false;
    State state_ = State::Detached;
    Descriptors received_descs_;
    std::vector<Event> events_;
    std::vector<std::byte> ctrl_rx_fifo_;
    std::vector<std::byte> reply_data_;
    std::vector<std::byte> bulk_out_fifo_;
    std::vector<std::byte> bulk_in_fifo_;
};

class MockStorage final : public StorageBackend {
public:
    bool present = true;
    uint64_t block_count = 2048; // 1 MB with 512B blocks
    unsigned int block_size = 512;
    std::map<uint64_t, std::vector<std::byte>> blocks;

    [[nodiscard]] mm::mcu::Status poll(bool& p) override {
        p = present;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status geometry(mm::mcu::StorageGeometry& geom) override {
        if (!present) return mm::mcu::Status::TransportError;
        geom.block_count = block_count;
        geom.block_size = block_size;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status read(std::uint64_t blk, std::span<std::byte> data) override {
        if (!present) return mm::mcu::Status::TransportError;
        if (data.size() != block_size) return mm::mcu::Status::BadArgument;
        auto it = blocks.find(blk);
        if (it != blocks.end()) {
            std::copy(it->second.begin(), it->second.end(), data.begin());
        } else {
            std::fill(data.begin(), data.end(), std::byte{0});
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status write(std::uint64_t blk, std::span<const std::byte> data) override {
        if (!present) return mm::mcu::Status::TransportError;
        if (data.size() != block_size) return mm::mcu::Status::BadArgument;
        blocks[blk].assign(data.begin(), data.end());
        return mm::mcu::Status::Ok;
    }
};

void test_descriptors_and_cbw_csw() {
    const auto descs = mm::usb::msc::default_descriptors();
    expect(descs.device.size() == 18, "msc device descriptor is 18 bytes");
    expect(descs.configuration.size() == 32, "msc configuration descriptor is 32 bytes");
    expect(descs.strings.size() == 4, "msc strings list has 4 entries");

    // Test CBW decode
    std::array<std::byte, 31> raw_cbw{};
    // Signature "USBC" -> 0x55, 0x53, 0x42, 0x43
    raw_cbw[0] = std::byte{0x55};
    raw_cbw[1] = std::byte{0x53};
    raw_cbw[2] = std::byte{0x42};
    raw_cbw[3] = std::byte{0x43};
    // Tag 0x12345678
    raw_cbw[4] = std::byte{0x78};
    raw_cbw[5] = std::byte{0x56};
    raw_cbw[6] = std::byte{0x34};
    raw_cbw[7] = std::byte{0x12};
    // Data transfer length 512
    raw_cbw[8] = std::byte{0x00};
    raw_cbw[9] = std::byte{0x02};
    raw_cbw[10] = std::byte{0x00};
    raw_cbw[11] = std::byte{0x00};
    // Flags: Data In (0x80)
    raw_cbw[12] = std::byte{0x80};
    // LUN 0
    raw_cbw[13] = std::byte{0x00};
    // CB Length 6
    raw_cbw[14] = std::byte{0x06};
    // CDB: 0x12 (INQUIRY), 0, 0, 0, 36, 0
    raw_cbw[15] = std::byte{0x12};
    raw_cbw[19] = std::byte{36};

    CommandBlockWrapper cbw{};
    expect(mm::usb::msc::decode_cbw(raw_cbw, cbw), "decode valid cbw ok");
    expect(cbw.signature == mm::usb::msc::CBW_SIGNATURE, "cbw signature matches");
    expect(cbw.tag == 0x12345678, "cbw tag matches");
    expect(cbw.data_transfer_length == 512, "cbw data transfer length matches");
    expect(cbw.flags == 0x80, "cbw flags match");
    expect(cbw.lun == 0, "cbw lun is 0");
    expect(cbw.cb_length == 6, "cb length is 6");
    expect(cbw.cb[0] == 0x12, "cdb op is INQUIRY");

    // Invalid signature
    raw_cbw[0] = std::byte{0x00};
    expect(!mm::usb::msc::decode_cbw(raw_cbw, cbw), "invalid signature rejected");

    // Test CSW encode
    CommandStatusWrapper csw{
        .signature = mm::usb::msc::CSW_SIGNATURE,
        .tag = 0x12345678,
        .data_residue = 0,
        .status = mm::usb::msc::CSW_STATUS_PASSED
    };
    auto enc = mm::usb::msc::encode_csw(csw);
    expect(enc.size() == 13, "csw encoded size is 13 bytes");
    expect(enc[0] == std::byte{0x55} && enc[1] == std::byte{0x53} &&
           enc[2] == std::byte{0x42} && enc[3] == std::byte{0x53}, "csw signature is USBS");
    expect(enc[4] == std::byte{0x78} && enc[5] == std::byte{0x56} &&
           enc[6] == std::byte{0x34} && enc[7] == std::byte{0x12}, "csw tag matches");
    expect(enc[12] == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "csw status is passed");
}

void test_msc_control_requests() {
    MockDevice dev;
    MockStorage storage;
    mm::usb::msc::MscDevice msc(dev, &storage);

    expect(msc.initialize() == Status::Ok, "msc initialize ok");
    expect(msc.attach() == Status::Ok, "msc attach ok");

    // GET_MAX_LUN (0xfe)
    const SetupPacket get_max_lun{
        .request_type = 0xa1, // Class, Interface, In
        .request = mm::usb::msc::MSC_REQUEST_GET_MAX_LUN,
        .value = 0,
        .index = 0,
        .length = 1
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = get_max_lun});
    expect(msc.poll() == Status::Ok, "poll GET_MAX_LUN ok");
    expect(dev.replied_, "GET_MAX_LUN replied");
    expect(dev.reply_data_.size() == 1, "reply data is 1 byte");
    expect(dev.reply_data_[0] == std::byte{0}, "max LUN is 0");

    // RESET (0xff)
    dev.replied_ = false;
    const SetupPacket msc_reset{
        .request_type = 0x21, // Class, Interface, Out
        .request = mm::usb::msc::MSC_REQUEST_RESET,
        .value = 0,
        .index = 0,
        .length = 0
    };
    dev.inject_event(Event{.kind = EventKind::Setup, .setup = msc_reset});
    expect(msc.poll() == Status::Ok, "poll RESET ok");
    expect(dev.replied_, "RESET replied");
    expect(dev.reply_data_.empty(), "RESET reply is empty (status only)");
}

void test_msc_inquiry_and_test_unit_ready() {
    MockDevice dev;
    MockStorage storage;
    mm::usb::msc::MscDevice msc(dev, &storage);
    (void)msc.initialize();
    (void)msc.attach();
    dev.set_state(State::Configured);

    // 1. TEST_UNIT_READY (0x00) with storage present
    std::array<std::byte, 31> tur_cbw{};
    tur_cbw[0] = std::byte{0x55}; tur_cbw[1] = std::byte{0x53};
    tur_cbw[2] = std::byte{0x42}; tur_cbw[3] = std::byte{0x43}; // "USBC"
    tur_cbw[4] = std::byte{0x01}; // Tag 1
    tur_cbw[14] = std::byte{6};   // CB Length 6
    tur_cbw[15] = std::byte{mm::usb::msc::SCSI_TEST_UNIT_READY};

    dev.inject_bulk_out_data(tur_cbw);
    expect(msc.poll() == Status::Ok, "poll TUR ok");

    // Should generate CSW on Bulk IN (13 bytes)
    expect(dev.bulk_in_fifo_.size() == 13, "TUR returned 13-byte CSW");
    expect(dev.bulk_in_fifo_[12] == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "TUR status passed");
    dev.bulk_in_fifo_.clear();

    // 2. INQUIRY (0x12)
    std::array<std::byte, 31> inq_cbw{};
    inq_cbw[0] = std::byte{0x55}; inq_cbw[1] = std::byte{0x53};
    inq_cbw[2] = std::byte{0x42}; inq_cbw[3] = std::byte{0x43}; // "USBC"
    inq_cbw[4] = std::byte{0x02}; // Tag 2
    inq_cbw[8] = std::byte{36};   // Transfer length 36
    inq_cbw[12] = std::byte{0x80}; // Data In
    inq_cbw[14] = std::byte{6};
    inq_cbw[15] = std::byte{mm::usb::msc::SCSI_INQUIRY};
    inq_cbw[19] = std::byte{36};  // Allocation length 36

    dev.inject_bulk_out_data(inq_cbw);
    // First poll receives CBW and begins data in phase
    expect(msc.poll() == Status::Ok, "poll INQUIRY data in ok");
    // Next poll transmits CSW
    expect(msc.poll() == Status::Ok, "poll INQUIRY csw ok");

    // Total sent: 36 bytes inquiry data + 13 bytes CSW = 49 bytes
    expect(dev.bulk_in_fifo_.size() == 49, "INQUIRY total 49 bytes in FIFO");
    expect(dev.bulk_in_fifo_[0] == std::byte{0x00}, "direct access device");
    expect(dev.bulk_in_fifo_[1] == std::byte{0x80}, "removable media");
    // Check vendor "32bitmic" at offset 8
    char vendor[9]{};
    for (int i = 0; i < 8; ++i) vendor[i] = static_cast<char>(dev.bulk_in_fifo_[8 + i]);
    expect(std::strcmp(vendor, "32bitmic") == 0, "vendor string matches");
    // CSW is at offset 36..48; status byte is at offset 48
    expect(dev.bulk_in_fifo_[48] == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "inquiry CSW passed");
    dev.bulk_in_fifo_.clear();
}

void test_msc_read_capacity_and_modesense() {
    MockDevice dev;
    MockStorage storage;
    storage.block_count = 1000;
    storage.block_size = 512;
    mm::usb::msc::MscDevice msc(dev, &storage);
    (void)msc.initialize();
    (void)msc.attach();
    dev.set_state(State::Configured);

    // READ_CAPACITY_10 (0x25)
    std::array<std::byte, 31> rc_cbw{};
    rc_cbw[0] = std::byte{0x55}; rc_cbw[1] = std::byte{0x53};
    rc_cbw[2] = std::byte{0x42}; rc_cbw[3] = std::byte{0x43};
    rc_cbw[4] = std::byte{0x03}; // Tag 3
    rc_cbw[8] = std::byte{8};    // Transfer length 8
    rc_cbw[12] = std::byte{0x80}; // Data In
    rc_cbw[14] = std::byte{10};
    rc_cbw[15] = std::byte{mm::usb::msc::SCSI_READ_CAPACITY_10};

    dev.inject_bulk_out_data(rc_cbw);
    expect(msc.poll() == Status::Ok, "poll READ CAPACITY data ok");
    expect(msc.poll() == Status::Ok, "poll READ CAPACITY csw ok");

    // 8 bytes data + 13 bytes CSW = 21 bytes
    expect(dev.bulk_in_fifo_.size() == 21, "READ CAPACITY sent 21 bytes");
    // Last LBA: 999 (0x000003e7) in big-endian
    expect(dev.bulk_in_fifo_[0] == std::byte{0} && dev.bulk_in_fifo_[1] == std::byte{0} &&
           dev.bulk_in_fifo_[2] == std::byte{0x03} && dev.bulk_in_fifo_[3] == std::byte{0xe7},
           "last LBA is 999");
    // Block size: 512 (0x00000200) in big-endian
    expect(dev.bulk_in_fifo_[4] == std::byte{0} && dev.bulk_in_fifo_[5] == std::byte{0} &&
           dev.bulk_in_fifo_[6] == std::byte{0x02} && dev.bulk_in_fifo_[7] == std::byte{0x00},
           "block size is 512");
    expect(dev.bulk_in_fifo_[20] == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "CSW passed");
    dev.bulk_in_fifo_.clear();

    // MODE_SENSE_6 (0x1a)
    std::array<std::byte, 31> ms_cbw{};
    ms_cbw[0] = std::byte{0x55}; ms_cbw[1] = std::byte{0x53};
    ms_cbw[2] = std::byte{0x42}; ms_cbw[3] = std::byte{0x43};
    ms_cbw[4] = std::byte{0x04}; // Tag 4
    ms_cbw[8] = std::byte{4};    // Transfer length 4
    ms_cbw[12] = std::byte{0x80}; // Data In
    ms_cbw[14] = std::byte{6};
    ms_cbw[15] = std::byte{mm::usb::msc::SCSI_MODE_SENSE_6};
    ms_cbw[19] = std::byte{4};   // Alloc length 4

    dev.inject_bulk_out_data(ms_cbw);
    expect(msc.poll() == Status::Ok, "poll MODE SENSE 6 data ok");
    expect(msc.poll() == Status::Ok, "poll MODE SENSE 6 csw ok");

    expect(dev.bulk_in_fifo_.size() == 17, "MODE SENSE 6 sent 17 bytes (4 data + 13 CSW)");
    expect(dev.bulk_in_fifo_[0] == std::byte{3}, "mode data length 3");
    expect(dev.bulk_in_fifo_[2] == std::byte{0}, "write protected flag is 0 (writable)");
    expect(dev.bulk_in_fifo_[16] == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "CSW passed");
    dev.bulk_in_fifo_.clear();
}

void test_msc_block_read_and_write() {
    MockDevice dev;
    MockStorage storage;
    mm::usb::msc::MscDevice msc(dev, &storage);
    (void)msc.initialize();
    (void)msc.attach();
    dev.set_state(State::Configured);

    // 1. WRITE_10: Write 1 block (512 bytes) to LBA 5
    std::array<std::byte, 31> wr_cbw{};
    wr_cbw[0] = std::byte{0x55}; wr_cbw[1] = std::byte{0x53};
    wr_cbw[2] = std::byte{0x42}; wr_cbw[3] = std::byte{0x43};
    wr_cbw[4] = std::byte{0x05}; // Tag 5
    wr_cbw[8] = std::byte{0x00}; wr_cbw[9] = std::byte{0x02}; // Transfer length 512
    wr_cbw[12] = std::byte{0x00}; // Data Out
    wr_cbw[14] = std::byte{10};
    wr_cbw[15] = std::byte{mm::usb::msc::SCSI_WRITE_10};
    // LBA 5 (bytes 17..20 in CBW)
    wr_cbw[17] = std::byte{0}; wr_cbw[18] = std::byte{0};
    wr_cbw[19] = std::byte{0}; wr_cbw[20] = std::byte{5};
    // Blocks: 1 (bytes 22..23 in CBW)
    wr_cbw[22] = std::byte{0}; wr_cbw[23] = std::byte{1};

    dev.inject_bulk_out_data(wr_cbw);
    // Inject 512 bytes payload
    std::vector<std::byte> payload(512);
    for (std::size_t i = 0; i < 512; ++i) payload[i] = static_cast<std::byte>(i & 0xff);
    dev.inject_bulk_out_data(payload);

    // Pump poll until write completes and CSW sent
    for (int i = 0; i < 5; ++i) {
        (void)msc.poll();
    }

    // Verify storage received block 5
    expect(storage.blocks.count(5) == 1, "block 5 written in storage");
    expect(storage.blocks[5] == payload, "block 5 content matches payload");
    // Verify CSW sent
    expect(!dev.bulk_in_fifo_.empty(), "CSW sent on bulk in");
    expect(dev.bulk_in_fifo_.back() == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "WRITE_10 CSW passed");
    dev.bulk_in_fifo_.clear();

    // 2. READ_10: Read back 1 block from LBA 5
    std::array<std::byte, 31> rd_cbw{};
    rd_cbw[0] = std::byte{0x55}; rd_cbw[1] = std::byte{0x53};
    rd_cbw[2] = std::byte{0x42}; rd_cbw[3] = std::byte{0x43};
    rd_cbw[4] = std::byte{0x06}; // Tag 6
    rd_cbw[8] = std::byte{0x00}; rd_cbw[9] = std::byte{0x02}; // Transfer length 512
    rd_cbw[12] = std::byte{0x80}; // Data In
    rd_cbw[14] = std::byte{10};
    rd_cbw[15] = std::byte{mm::usb::msc::SCSI_READ_10};
    // LBA 5
    rd_cbw[17] = std::byte{0}; rd_cbw[18] = std::byte{0};
    rd_cbw[19] = std::byte{0}; rd_cbw[20] = std::byte{5};
    // Blocks: 1
    rd_cbw[22] = std::byte{0}; rd_cbw[23] = std::byte{1};

    dev.inject_bulk_out_data(rd_cbw);

    // Pump poll
    for (int i = 0; i < 5; ++i) {
        (void)msc.poll();
    }

    // Total sent: 512 bytes read data + 13 bytes CSW = 525 bytes
    expect(dev.bulk_in_fifo_.size() == 525, "READ_10 returned 525 bytes total");
    std::vector<std::byte> read_back(dev.bulk_in_fifo_.begin(), dev.bulk_in_fifo_.begin() + 512);
    expect(read_back == payload, "read-back data matches previously written payload");
    expect(dev.bulk_in_fifo_.back() == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "READ_10 CSW passed");
    dev.bulk_in_fifo_.clear();
}

void test_msc_request_sense_and_error_handling() {
    MockDevice dev;
    MockStorage storage;
    mm::usb::msc::MscDevice msc(dev, &storage);
    (void)msc.initialize();
    (void)msc.attach();
    dev.set_state(State::Configured);

    // Send unknown command 0x99
    std::array<std::byte, 31> bad_cbw{};
    bad_cbw[0] = std::byte{0x55}; bad_cbw[1] = std::byte{0x53};
    bad_cbw[2] = std::byte{0x42}; bad_cbw[3] = std::byte{0x43};
    bad_cbw[4] = std::byte{0x07}; // Tag 7
    bad_cbw[14] = std::byte{6};
    bad_cbw[15] = std::byte{0x99}; // Unknown SCSI op

    dev.inject_bulk_out_data(bad_cbw);
    expect(msc.poll() == Status::Ok, "poll bad command ok");

    expect(dev.bulk_in_fifo_.size() == 13, "CSW sent for bad command");
    expect(dev.bulk_in_fifo_[12] == std::byte{mm::usb::msc::CSW_STATUS_FAILED}, "bad command CSW status failed");
    dev.bulk_in_fifo_.clear();

    // REQUEST_SENSE (0x03)
    std::array<std::byte, 31> sense_cbw{};
    sense_cbw[0] = std::byte{0x55}; sense_cbw[1] = std::byte{0x53};
    sense_cbw[2] = std::byte{0x42}; sense_cbw[3] = std::byte{0x43};
    sense_cbw[4] = std::byte{0x08}; // Tag 8
    sense_cbw[8] = std::byte{18};   // Transfer length 18
    sense_cbw[12] = std::byte{0x80}; // Data In
    sense_cbw[14] = std::byte{6};
    sense_cbw[15] = std::byte{mm::usb::msc::SCSI_REQUEST_SENSE};
    sense_cbw[19] = std::byte{18};  // Alloc length 18

    dev.inject_bulk_out_data(sense_cbw);
    expect(msc.poll() == Status::Ok, "poll REQUEST SENSE data ok");
    expect(msc.poll() == Status::Ok, "poll REQUEST SENSE csw ok");

    // 18 bytes sense data + 13 bytes CSW = 31 bytes
    expect(dev.bulk_in_fifo_.size() == 31, "REQUEST SENSE returned 31 bytes");
    expect(dev.bulk_in_fifo_[0] == std::byte{0x70}, "response code 0x70");
    expect(dev.bulk_in_fifo_[2] == std::byte{mm::usb::msc::SENSE_ILLEGAL_REQUEST}, "sense key is ILLEGAL REQUEST (0x05)");
    expect(dev.bulk_in_fifo_[12] == std::byte{mm::usb::msc::ASC_INVALID_COMMAND_OPERATION_CODE}, "asc is 0x20");
    expect(dev.bulk_in_fifo_[30] == std::byte{mm::usb::msc::CSW_STATUS_PASSED}, "REQUEST SENSE CSW passed");
}

const mm::test::case_ cases[] = {
    {"descriptors and cbw csw", &test_descriptors_and_cbw_csw},
    {"control requests", &test_msc_control_requests},
    {"inquiry and test unit ready", &test_msc_inquiry_and_test_unit_ready},
    {"read capacity and mode sense", &test_msc_read_capacity_and_modesense},
    {"block read and write", &test_msc_block_read_and_write},
    {"request sense and error handling", &test_msc_request_sense_and_error_handling},
};

const mm::test::registrar reg{"mm.usb.msc", cases};

}  // namespace
