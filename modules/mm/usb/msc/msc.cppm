// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

export module mm.usb.msc;

import mm.mcu;
import mm.usb;
import mm.usb.device;

export namespace mm::usb::msc {

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
constexpr uint16_t PRODUCT_ID = 0x6152;

constexpr EndpointAddress ENDPOINT_BULK_OUT{0x01};
constexpr EndpointAddress ENDPOINT_BULK_IN{0x81};

constexpr uint8_t MSC_REQUEST_GET_MAX_LUN = 0xfe;
constexpr uint8_t MSC_REQUEST_RESET = 0xff;

constexpr uint32_t CBW_SIGNATURE = 0x43425355; // "USBC"
constexpr uint32_t CSW_SIGNATURE = 0x53425355; // "USBS"

constexpr uint8_t CSW_STATUS_PASSED = 0x00;
constexpr uint8_t CSW_STATUS_FAILED = 0x01;
constexpr uint8_t CSW_STATUS_PHASE_ERROR = 0x02;

// SCSI Transparent Command Set (SPC-2 / SBC-2)
constexpr uint8_t SCSI_TEST_UNIT_READY = 0x00;
constexpr uint8_t SCSI_REQUEST_SENSE = 0x03;
constexpr uint8_t SCSI_INQUIRY = 0x12;
constexpr uint8_t SCSI_MODE_SENSE_6 = 0x1a;
constexpr uint8_t SCSI_START_STOP_UNIT = 0x1b;
constexpr uint8_t SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL = 0x1e;
constexpr uint8_t SCSI_READ_CAPACITY_10 = 0x25;
constexpr uint8_t SCSI_READ_10 = 0x28;
constexpr uint8_t SCSI_WRITE_10 = 0x2a;
constexpr uint8_t SCSI_VERIFY_10 = 0x2f;
constexpr uint8_t SCSI_SYNCHRONIZE_CACHE_10 = 0x35;
constexpr uint8_t SCSI_MODE_SENSE_10 = 0x5a;

// Sense Keys
constexpr uint8_t SENSE_NONE = 0x00;
constexpr uint8_t SENSE_NOT_READY = 0x02;
constexpr uint8_t SENSE_MEDIUM_ERROR = 0x03;
constexpr uint8_t SENSE_ILLEGAL_REQUEST = 0x05;
constexpr uint8_t SENSE_UNIT_ATTENTION = 0x06;

// Additional Sense Codes (ASC)
constexpr uint8_t ASC_NONE = 0x00;
constexpr uint8_t ASC_LOGICAL_UNIT_NOT_READY = 0x04;
constexpr uint8_t ASC_INVALID_COMMAND_OPERATION_CODE = 0x20;
constexpr uint8_t ASC_LBA_OUT_OF_RANGE = 0x21;
constexpr uint8_t ASC_INVALID_FIELD_IN_CDB = 0x24;
constexpr uint8_t ASC_WRITE_PROTECTED = 0x27;
constexpr uint8_t ASC_MEDIUM_NOT_PRESENT = 0x3a;

struct CommandBlockWrapper {
    uint32_t signature = 0;
    uint32_t tag = 0;
    uint32_t data_transfer_length = 0;
    uint8_t flags = 0;
    uint8_t lun = 0;
    uint8_t cb_length = 0;
    std::array<uint8_t, 16> cb{};

    constexpr bool operator==(const CommandBlockWrapper&) const = default;
};

struct CommandStatusWrapper {
    uint32_t signature = CSW_SIGNATURE;
    uint32_t tag = 0;
    uint32_t data_residue = 0;
    uint8_t status = CSW_STATUS_PASSED;

    constexpr bool operator==(const CommandStatusWrapper&) const = default;
};

inline bool decode_cbw(std::span<const std::byte> bytes, CommandBlockWrapper& cbw) {
    if (bytes.size() < 31) return false;
    cbw.signature = static_cast<uint32_t>(bytes[0]) |
                    (static_cast<uint32_t>(bytes[1]) << 8) |
                    (static_cast<uint32_t>(bytes[2]) << 16) |
                    (static_cast<uint32_t>(bytes[3]) << 24);
    if (cbw.signature != CBW_SIGNATURE) return false;

    cbw.tag = static_cast<uint32_t>(bytes[4]) |
              (static_cast<uint32_t>(bytes[5]) << 8) |
              (static_cast<uint32_t>(bytes[6]) << 16) |
              (static_cast<uint32_t>(bytes[7]) << 24);
    cbw.data_transfer_length = static_cast<uint32_t>(bytes[8]) |
                               (static_cast<uint32_t>(bytes[9]) << 8) |
                               (static_cast<uint32_t>(bytes[10]) << 16) |
                               (static_cast<uint32_t>(bytes[11]) << 24);
    cbw.flags = static_cast<uint8_t>(bytes[12]);
    cbw.lun = static_cast<uint8_t>(bytes[13]) & 0x0f;
    cbw.cb_length = static_cast<uint8_t>(bytes[14]) & 0x1f;
    if (cbw.cb_length > 16) return false;

    for (std::size_t i = 0; i < 16; ++i) {
        cbw.cb[i] = static_cast<uint8_t>(bytes[15 + i]);
    }
    return true;
}

inline std::array<std::byte, 13> encode_csw(const CommandStatusWrapper& csw) {
    return {
        static_cast<std::byte>(csw.signature & 0xff),
        static_cast<std::byte>((csw.signature >> 8) & 0xff),
        static_cast<std::byte>((csw.signature >> 16) & 0xff),
        static_cast<std::byte>((csw.signature >> 24) & 0xff),
        static_cast<std::byte>(csw.tag & 0xff),
        static_cast<std::byte>((csw.tag >> 8) & 0xff),
        static_cast<std::byte>((csw.tag >> 16) & 0xff),
        static_cast<std::byte>((csw.tag >> 24) & 0xff),
        static_cast<std::byte>(csw.data_residue & 0xff),
        static_cast<std::byte>((csw.data_residue >> 8) & 0xff),
        static_cast<std::byte>((csw.data_residue >> 16) & 0xff),
        static_cast<std::byte>((csw.data_residue >> 24) & 0xff),
        static_cast<std::byte>(csw.status)
    };
}

class StorageBackend {
public:
    virtual ~StorageBackend() = default;
    [[nodiscard]] virtual mm::mcu::Status poll(bool& present) = 0;
    [[nodiscard]] virtual mm::mcu::Status geometry(mm::mcu::StorageGeometry& geom) = 0;
    [[nodiscard]] virtual mm::mcu::Status read(std::uint64_t block, std::span<std::byte> data) = 0;
    [[nodiscard]] virtual mm::mcu::Status write(std::uint64_t block, std::span<const std::byte> data) = 0;
};

class McuStorageBackend final : public StorageBackend {
public:
    [[nodiscard]] mm::mcu::Status poll(bool& present) override {
        return mm::mcu::storage_poll(present);
    }
    [[nodiscard]] mm::mcu::Status geometry(mm::mcu::StorageGeometry& geom) override {
        return mm::mcu::storage_geometry(geom);
    }
    [[nodiscard]] mm::mcu::Status read(std::uint64_t block, std::span<std::byte> data) override {
        return mm::mcu::storage_read(block, data);
    }
    [[nodiscard]] mm::mcu::Status write(std::uint64_t block, std::span<const std::byte> data) override {
        return mm::mcu::storage_write(block, data);
    }
};

inline const std::vector<std::byte>& default_device_descriptor() {
    static const std::vector<std::byte> desc = {
        std::byte{18},                // bLength
        std::byte{1},                 // bDescriptorType (DEVICE)
        std::byte{0x00}, std::byte{0x02}, // bcdUSB 2.00
        std::byte{0},                 // bDeviceClass
        std::byte{0},                 // bDeviceSubClass
        std::byte{0},                 // bDeviceProtocol
        std::byte{64},                // bMaxPacketSize0
        std::byte{0x50}, std::byte{0x1d}, // idVendor 0x1d50
        std::byte{0x52}, std::byte{0x61}, // idProduct 0x6152 (MSC)
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

        // Interface 0: Mass Storage (9 bytes)
        std::byte{9},                 // bLength
        std::byte{4},                 // bDescriptorType (INTERFACE)
        std::byte{0},                 // bInterfaceNumber
        std::byte{0},                 // bAlternateSetting
        std::byte{2},                 // bNumEndpoints
        std::byte{0x08},              // bInterfaceClass (Mass Storage)
        std::byte{0x06},              // bInterfaceSubClass (SCSI Transparent)
        std::byte{0x50},              // bInterfaceProtocol (Bulk-Only Transport)
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
        std::byte{34}, std::byte{3},
        std::byte{'U'}, std::byte{0},
        std::byte{'S'}, std::byte{0},
        std::byte{'B'}, std::byte{0},
        std::byte{' '}, std::byte{0},
        std::byte{'M'}, std::byte{0},
        std::byte{'a'}, std::byte{0},
        std::byte{'s'}, std::byte{0},
        std::byte{'s'}, std::byte{0},
        std::byte{' '}, std::byte{0},
        std::byte{'S'}, std::byte{0},
        std::byte{'t'}, std::byte{0},
        std::byte{'o'}, std::byte{0},
        std::byte{'r'}, std::byte{0},
        std::byte{'a'}, std::byte{0},
        std::byte{'g'}, std::byte{0},
        std::byte{'e'}, std::byte{0}
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

class MscDevice {
public:
    explicit MscDevice(Device& device, StorageBackend* storage = nullptr)
        : device_(device),
          storage_(storage ? storage : &default_mcu_storage_) {}

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

    [[nodiscard]] Status poll() {
        // Handle bus events and class control requests
        Event ev;
        const Status ev_status = device_.take_event(ev);
        if (ev_status == Status::Ok) {
            if (ev.kind == EventKind::Setup) {
                if (ev.setup.type() == Type::Class && ev.setup.recipient() == Recipient::Interface) {
                    switch (ev.setup.request) {
                        case MSC_REQUEST_GET_MAX_LUN: {
                            static const std::array<std::byte, 1> max_lun = {std::byte{0}};
                            (void)device_.control_reply(max_lun);
                            break;
                        }
                        case MSC_REQUEST_RESET: {
                            reset_bot();
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
                reset_bot();
            }
        }

        if (device_.state() != State::Configured) return Status::Ok;

        // One poll carries a command as far as it can go without waiting: the
        // CBW, its data phase, and its CSW, so a command with no data phase is
        // answered by the poll that read it. A step that leaves the state
        // unchanged made no progress (no bytes yet, or the endpoint is busy)
        // and ends the poll; the bound is only a guard.
        for (int pass = 0; pass < 8; ++pass) {
            const BotState before = bot_state_;
            step();
            if (bot_state_ == before) break;
        }

        return Status::Ok;
    }

private:
    // Advances the Bulk-Only Transport state machine by one step.
    void step() {
        switch (bot_state_) {
            case BotState::WaitCbw: {
                std::size_t received = 0;
                std::span<std::byte> target(
                    cbw_bytes_.data() + cbw_received_, 31 - cbw_received_);
                const Status r = device_.read(ENDPOINT_BULK_OUT, target, received);
                if (r == Status::Ok && received > 0) {
                    cbw_received_ += received;
                    if (cbw_received_ == 31) {
                        if (decode_cbw(cbw_bytes_, current_cbw_)) {
                            process_cbw();
                        } else {
                            // Invalid CBW signature or length -> stall endpoints
                            (void)device_.stall(ENDPOINT_BULK_IN, true);
                            (void)device_.stall(ENDPOINT_BULK_OUT, true);
                            reset_bot();
                        }
                    }
                }
                break;
            }

            case BotState::DataIn: {
                if (data_offset_ < data_buffer_len_) {
                    std::size_t written = 0;
                    std::span<const std::byte> chunk(
                        data_buffer_.data() + data_offset_,
                        data_buffer_len_ - data_offset_);
                    const Status w = device_.write(ENDPOINT_BULK_IN, chunk, written);
                    if (w == Status::Ok && written > 0) {
                        data_offset_ += written;
                    }
                }
                if (data_offset_ >= data_buffer_len_) {
                    prepare_csw(CSW_STATUS_PASSED);
                }
                break;
            }

            case BotState::ReadBlocks: {
                // Read next block from storage if buffer drained
                if (block_buf_pos_ >= block_buf_size_) {
                    if (remaining_blocks_ > 0) {
                        mm::mcu::StorageGeometry geom{};
                        (void)storage_->geometry(geom);
                        const unsigned int bs = geom.block_size ? geom.block_size : 512;
                        block_buf_size_ = bs <= block_buffer_.size() ? bs : block_buffer_.size();
                        block_buf_pos_ = 0;

                        const mm::mcu::Status sr = storage_->read(
                            current_lba_, std::span{block_buffer_.data(), block_buf_size_});
                        if (sr != mm::mcu::Status::Ok) {
                            sense_key_ = SENSE_MEDIUM_ERROR;
                            asc_ = ASC_NONE;
                            prepare_csw(CSW_STATUS_FAILED);
                            break;
                        }
                        current_lba_++;
                        remaining_blocks_--;
                    } else {
                        prepare_csw(CSW_STATUS_PASSED);
                        break;
                    }
                }

                // Send chunk from current block buffer
                if (block_buf_pos_ < block_buf_size_) {
                    std::size_t written = 0;
                    std::span<const std::byte> chunk(
                        block_buffer_.data() + block_buf_pos_,
                        block_buf_size_ - block_buf_pos_);
                    const Status w = device_.write(ENDPOINT_BULK_IN, chunk, written);
                    if (w == Status::Ok && written > 0) {
                        block_buf_pos_ += written;
                        if (data_residue_ >= written) data_residue_ -= written;
                    }
                }

                if (block_buf_pos_ >= block_buf_size_ && remaining_blocks_ == 0) {
                    prepare_csw(CSW_STATUS_PASSED);
                }
                break;
            }

            case BotState::WriteBlocks: {
                // Receive bytes into block buffer
                if (block_buf_pos_ < block_buf_size_) {
                    std::size_t received = 0;
                    std::span<std::byte> chunk(
                        block_buffer_.data() + block_buf_pos_,
                        block_buf_size_ - block_buf_pos_);
                    const Status r = device_.read(ENDPOINT_BULK_OUT, chunk, received);
                    if (r == Status::Ok && received > 0) {
                        block_buf_pos_ += received;
                        if (data_residue_ >= received) data_residue_ -= received;
                    }
                }

                // If block buffer is full, write to storage
                if (block_buf_pos_ >= block_buf_size_) {
                    const mm::mcu::Status sw = storage_->write(
                        current_lba_, std::span{block_buffer_.data(), block_buf_size_});
                    if (sw != mm::mcu::Status::Ok) {
                        sense_key_ = SENSE_MEDIUM_ERROR;
                        asc_ = ASC_NONE;
                        prepare_csw(CSW_STATUS_FAILED);
                        break;
                    }
                    current_lba_++;
                    remaining_blocks_--;
                    block_buf_pos_ = 0;

                    if (remaining_blocks_ == 0) {
                        prepare_csw(CSW_STATUS_PASSED);
                    }
                }
                break;
            }

            case BotState::SendCsw: {
                std::size_t written = 0;
                std::span<const std::byte> chunk(
                    csw_bytes_.data() + csw_sent_, 13 - csw_sent_);
                const Status w = device_.write(ENDPOINT_BULK_IN, chunk, written);
                if (w == Status::Ok && written > 0) {
                    csw_sent_ += written;
                    if (csw_sent_ == 13) {
                        reset_bot();
                    }
                }
                break;
            }
        }
    }

    enum class BotState {
        WaitCbw,
        DataIn,
        ReadBlocks,
        WriteBlocks,
        SendCsw
    };

    void reset_bot() {
        bot_state_ = BotState::WaitCbw;
        cbw_received_ = 0;
        csw_sent_ = 0;
        data_offset_ = 0;
        data_buffer_len_ = 0;
        block_buf_pos_ = 0;
        block_buf_size_ = 0;
    }

    void prepare_csw(uint8_t status) {
        CommandStatusWrapper csw{
            .signature = CSW_SIGNATURE,
            .tag = current_cbw_.tag,
            .data_residue = data_residue_,
            .status = status
        };
        csw_bytes_ = encode_csw(csw);
        csw_sent_ = 0;
        bot_state_ = BotState::SendCsw;
    }

    void process_cbw() {
        const uint8_t op = current_cbw_.cb[0];
        data_residue_ = current_cbw_.data_transfer_length;

        switch (op) {
            case SCSI_TEST_UNIT_READY: {
                bool present = false;
                const mm::mcu::Status s = storage_->poll(present);
                if (s == mm::mcu::Status::Ok && present) {
                    prepare_csw(CSW_STATUS_PASSED);
                } else {
                    sense_key_ = SENSE_NOT_READY;
                    asc_ = ASC_MEDIUM_NOT_PRESENT;
                    prepare_csw(CSW_STATUS_FAILED);
                }
                break;
            }

            case SCSI_INQUIRY: {
                const uint32_t alloc_len = current_cbw_.cb[4];
                // 36 bytes standard SCSI Inquiry
                std::array<std::byte, 36> inq{};
                inq[0] = std::byte{0x00}; // Direct access block device
                inq[1] = std::byte{0x80}; // Removable
                inq[2] = std::byte{0x02}; // ANSI SCSI-2
                inq[3] = std::byte{0x02}; // Response format
                inq[4] = std::byte{31};   // Additional length (36 - 5)
                // Vendor: "32bitmic"
                constexpr char vendor[] = "32bitmic";
                for (std::size_t i = 0; i < 8; ++i) inq[8 + i] = static_cast<std::byte>(vendor[i]);
                // Product: "Modules.cpp Disk"
                constexpr char prod[] = "Modules.cpp Disk";
                for (std::size_t i = 0; i < 16; ++i) inq[16 + i] = static_cast<std::byte>(prod[i]);
                // Revision: "1.00"
                constexpr char rev[] = "1.00";
                for (std::size_t i = 0; i < 4; ++i) inq[32 + i] = static_cast<std::byte>(rev[i]);

                const std::size_t send_len = std::min(static_cast<std::size_t>(alloc_len), inq.size());
                std::memcpy(data_buffer_.data(), inq.data(), send_len);
                data_buffer_len_ = send_len;
                data_offset_ = 0;
                if (data_residue_ >= send_len) data_residue_ -= send_len;
                bot_state_ = BotState::DataIn;
                break;
            }

            case SCSI_REQUEST_SENSE: {
                const uint32_t alloc_len = current_cbw_.cb[4];
                std::array<std::byte, 18> sense{};
                sense[0] = std::byte{0x70}; // Current errors
                sense[2] = static_cast<std::byte>(sense_key_);
                sense[7] = std::byte{10};   // Additional sense length
                sense[12] = static_cast<std::byte>(asc_);
                sense[13] = static_cast<std::byte>(ascq_);

                // Clear sense
                sense_key_ = SENSE_NONE;
                asc_ = ASC_NONE;
                ascq_ = 0;

                const std::size_t send_len = std::min(static_cast<std::size_t>(alloc_len), sense.size());
                std::memcpy(data_buffer_.data(), sense.data(), send_len);
                data_buffer_len_ = send_len;
                data_offset_ = 0;
                if (data_residue_ >= send_len) data_residue_ -= send_len;
                bot_state_ = BotState::DataIn;
                break;
            }

            case SCSI_READ_CAPACITY_10: {
                mm::mcu::StorageGeometry geom{};
                const mm::mcu::Status s = storage_->geometry(geom);
                if (s == mm::mcu::Status::Ok && geom.block_count > 0) {
                    const uint32_t last_lba = static_cast<uint32_t>(geom.block_count - 1);
                    const uint32_t bs = geom.block_size ? geom.block_size : 512;
                    data_buffer_[0] = static_cast<std::byte>((last_lba >> 24) & 0xff);
                    data_buffer_[1] = static_cast<std::byte>((last_lba >> 16) & 0xff);
                    data_buffer_[2] = static_cast<std::byte>((last_lba >> 8) & 0xff);
                    data_buffer_[3] = static_cast<std::byte>(last_lba & 0xff);
                    data_buffer_[4] = static_cast<std::byte>((bs >> 24) & 0xff);
                    data_buffer_[5] = static_cast<std::byte>((bs >> 16) & 0xff);
                    data_buffer_[6] = static_cast<std::byte>((bs >> 8) & 0xff);
                    data_buffer_[7] = static_cast<std::byte>(bs & 0xff);
                    data_buffer_len_ = 8;
                    data_offset_ = 0;
                    if (data_residue_ >= 8) data_residue_ -= 8;
                    bot_state_ = BotState::DataIn;
                } else {
                    sense_key_ = SENSE_NOT_READY;
                    asc_ = ASC_MEDIUM_NOT_PRESENT;
                    prepare_csw(CSW_STATUS_FAILED);
                }
                break;
            }

            case SCSI_READ_10: {
                const uint32_t lba = (static_cast<uint32_t>(current_cbw_.cb[2]) << 24) |
                                     (static_cast<uint32_t>(current_cbw_.cb[3]) << 16) |
                                     (static_cast<uint32_t>(current_cbw_.cb[4]) << 8) |
                                     static_cast<uint32_t>(current_cbw_.cb[5]);
                const uint16_t blocks = (static_cast<uint16_t>(current_cbw_.cb[7]) << 8) |
                                        static_cast<uint16_t>(current_cbw_.cb[8]);

                current_lba_ = lba;
                remaining_blocks_ = blocks;
                block_buf_pos_ = 0;
                block_buf_size_ = 0;
                bot_state_ = BotState::ReadBlocks;
                break;
            }

            case SCSI_WRITE_10: {
                const uint32_t lba = (static_cast<uint32_t>(current_cbw_.cb[2]) << 24) |
                                     (static_cast<uint32_t>(current_cbw_.cb[3]) << 16) |
                                     (static_cast<uint32_t>(current_cbw_.cb[4]) << 8) |
                                     static_cast<uint32_t>(current_cbw_.cb[5]);
                const uint16_t blocks = (static_cast<uint16_t>(current_cbw_.cb[7]) << 8) |
                                        static_cast<uint16_t>(current_cbw_.cb[8]);

                current_lba_ = lba;
                remaining_blocks_ = blocks;
                mm::mcu::StorageGeometry geom{};
                (void)storage_->geometry(geom);
                const unsigned int bs = geom.block_size ? geom.block_size : 512;
                block_buf_size_ = bs <= block_buffer_.size() ? bs : block_buffer_.size();
                block_buf_pos_ = 0;
                bot_state_ = BotState::WriteBlocks;
                break;
            }

            case SCSI_MODE_SENSE_6: {
                // 4-byte header: Mode Data Length (3), Medium Type (0), Device Specific (0 - writable), Block Desc Len (0)
                data_buffer_[0] = std::byte{3};
                data_buffer_[1] = std::byte{0};
                data_buffer_[2] = std::byte{0};
                data_buffer_[3] = std::byte{0};
                const uint32_t alloc_len = current_cbw_.cb[4];
                const std::size_t send_len = std::min(static_cast<std::size_t>(alloc_len), std::size_t{4});
                data_buffer_len_ = send_len;
                data_offset_ = 0;
                if (data_residue_ >= send_len) data_residue_ -= send_len;
                bot_state_ = BotState::DataIn;
                break;
            }

            case SCSI_MODE_SENSE_10: {
                // 8-byte header
                data_buffer_[0] = std::byte{0};
                data_buffer_[1] = std::byte{6};
                data_buffer_[2] = std::byte{0};
                data_buffer_[3] = std::byte{0};
                data_buffer_[4] = std::byte{0};
                data_buffer_[5] = std::byte{0};
                data_buffer_[6] = std::byte{0};
                data_buffer_[7] = std::byte{0};
                const uint32_t alloc_len = (static_cast<uint32_t>(current_cbw_.cb[7]) << 8) |
                                           static_cast<uint32_t>(current_cbw_.cb[8]);
                const std::size_t send_len = std::min(static_cast<std::size_t>(alloc_len), std::size_t{8});
                data_buffer_len_ = send_len;
                data_offset_ = 0;
                if (data_residue_ >= send_len) data_residue_ -= send_len;
                bot_state_ = BotState::DataIn;
                break;
            }

            case SCSI_START_STOP_UNIT:
            case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
            case SCSI_VERIFY_10:
            case SCSI_SYNCHRONIZE_CACHE_10: {
                prepare_csw(CSW_STATUS_PASSED);
                break;
            }

            default: {
                sense_key_ = SENSE_ILLEGAL_REQUEST;
                asc_ = ASC_INVALID_COMMAND_OPERATION_CODE;
                ascq_ = 0;
                if (current_cbw_.data_transfer_length > 0) {
                    const EndpointAddress ep = (current_cbw_.flags & 0x80)
                        ? ENDPOINT_BULK_IN : ENDPOINT_BULK_OUT;
                    (void)device_.stall(ep, true);
                }
                prepare_csw(CSW_STATUS_FAILED);
                break;
            }
        }
    }

    Device& device_;
    McuStorageBackend default_mcu_storage_;
    StorageBackend* storage_;

    BotState bot_state_ = BotState::WaitCbw;
    std::array<std::byte, 31> cbw_bytes_{};
    std::size_t cbw_received_ = 0;
    CommandBlockWrapper current_cbw_{};

    std::array<std::byte, 13> csw_bytes_{};
    std::size_t csw_sent_ = 0;

    std::array<std::byte, 64> data_buffer_{};
    std::size_t data_buffer_len_ = 0;
    std::size_t data_offset_ = 0;
    uint32_t data_residue_ = 0;

    // Block I/O state
    std::array<std::byte, 512> block_buffer_{};
    uint32_t current_lba_ = 0;
    uint16_t remaining_blocks_ = 0;
    std::size_t block_buf_pos_ = 0;
    std::size_t block_buf_size_ = 0;

    // Sense state
    uint8_t sense_key_ = SENSE_NONE;
    uint8_t asc_ = ASC_NONE;
    uint8_t ascq_ = 0;
};

}  // namespace mm::usb::msc
