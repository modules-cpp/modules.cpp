// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <linux/aio_abi.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module platform.linux.usb.device;

import mm.usb;
import mm.usb.device;
import platform.linux.map;

export namespace platform::linux::usb::device {

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

inline int aio_setup(unsigned nr_events, aio_context_t* ctx_idp) {
    return static_cast<int>(::syscall(SYS_io_setup, nr_events, ctx_idp));
}

inline int aio_destroy(aio_context_t ctx_id) {
    return static_cast<int>(::syscall(SYS_io_destroy, ctx_id));
}

inline int aio_submit(aio_context_t ctx_id, long nr, struct iocb** iocbpp) {
    return static_cast<int>(::syscall(SYS_io_submit, ctx_id, nr, iocbpp));
}

inline int aio_getevents(aio_context_t ctx_id, long min_nr, long nr,
                         struct io_event* events, struct timespec* timeout) {
    return static_cast<int>(::syscall(SYS_io_getevents, ctx_id, min_nr, nr, events, timeout));
}

inline void write_le32(std::vector<std::byte>& buf, uint32_t val) {
    buf.push_back(static_cast<std::byte>(val & 0xff));
    buf.push_back(static_cast<std::byte>((val >> 8) & 0xff));
    buf.push_back(static_cast<std::byte>((val >> 16) & 0xff));
    buf.push_back(static_cast<std::byte>((val >> 24) & 0xff));
}

inline void write_le16(std::vector<std::byte>& buf, uint16_t val) {
    buf.push_back(static_cast<std::byte>(val & 0xff));
    buf.push_back(static_cast<std::byte>((val >> 8) & 0xff));
}

[[nodiscard]] inline bool parse_configfs_id(std::string_view text, uint16_t& id) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
    if (text.empty()) return false;
    unsigned long val = 0;
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    if (text.empty()) return false;
    for (char c : text) {
        int digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        val = val * static_cast<unsigned long>(base) + static_cast<unsigned long>(digit);
        if (val > 0xffffUL) return false;
    }
    id = static_cast<uint16_t>(val);
    return true;
}

[[nodiscard]] inline Status build_functionfs_descriptors(
    const Descriptors& descs, std::vector<std::byte>& out) {
    out.clear();
    std::span<const std::byte> sub = descs.configuration;
    if (sub.empty()) return Status::BadArgument;

    // Skip configuration header (type 2) if present
    if (sub.size() >= 2 && static_cast<uint8_t>(sub[1]) == 2 /* USB_DT_CONFIG */) {
        const uint8_t cfg_len = static_cast<uint8_t>(sub[0]);
        if (cfg_len < 9 || sub.size() < cfg_len) return Status::BadArgument;
        sub = sub.subspan(cfg_len);
    }

    // Count subordinate descriptors and validate structure
    size_t offset = 0;
    uint32_t count = 0;
    while (offset < sub.size()) {
        const uint8_t len = static_cast<uint8_t>(sub[offset]);
        if (len < 2 || offset + len > sub.size()) return Status::BadArgument;
        ++count;
        offset += len;
    }
    if (count == 0) return Status::BadArgument;

    const uint32_t header_len = 20; // 12 (head_v2) + 4 (fs_count) + 4 (hs_count)
    const uint32_t total_len = header_len + static_cast<uint32_t>(sub.size() * 2);

    out.reserve(total_len);
    write_le32(out, FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    write_le32(out, total_len);
    write_le32(out, FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC);
    write_le32(out, count);
    write_le32(out, count);

    // Full-speed copy
    for (std::byte b : sub) out.push_back(b);
    // High-speed copy
    for (std::byte b : sub) out.push_back(b);

    return Status::Ok;
}

[[nodiscard]] inline Status build_functionfs_strings(
    const Descriptors& descs, std::vector<std::byte>& out) {
    out.clear();
    if (descs.strings.empty()) {
        out.reserve(16);
        write_le32(out, FUNCTIONFS_STRINGS_MAGIC);
        write_le32(out, 16);
        write_le32(out, 0);
        write_le32(out, 0);
        return Status::Ok;
    }

    uint16_t lang = 0x0409; // en-US default
    if (!descs.strings.empty() && descs.strings[0].size() >= 4 &&
        static_cast<uint8_t>(descs.strings[0][1]) == 3 /* USB_DT_STRING */) {
        lang = static_cast<uint8_t>(descs.strings[0][2]) |
               (static_cast<uint16_t>(static_cast<uint8_t>(descs.strings[0][3])) << 8);
    }

    const uint32_t str_count = descs.strings.size() > 1 ? static_cast<uint32_t>(descs.strings.size() - 1) : 0;
    std::vector<std::byte> str_payload;

    for (size_t i = 1; i < descs.strings.size(); ++i) {
        const auto str_desc = descs.strings[i];
        if (str_desc.size() < 2 || static_cast<uint8_t>(str_desc[1]) != 3) {
            str_payload.push_back(static_cast<std::byte>(0));
            continue;
        }
        for (size_t k = 2; k + 1 < str_desc.size(); k += 2) {
            const uint16_t ch = static_cast<uint8_t>(str_desc[k]) |
                                (static_cast<uint16_t>(static_cast<uint8_t>(str_desc[k + 1])) << 8);
            if (ch < 0x80) {
                str_payload.push_back(static_cast<std::byte>(ch));
            } else if (ch < 0x800) {
                str_payload.push_back(static_cast<std::byte>(0xc0 | (ch >> 6)));
                str_payload.push_back(static_cast<std::byte>(0x80 | (ch & 0x3f)));
            } else {
                str_payload.push_back(static_cast<std::byte>(0xe0 | (ch >> 12)));
                str_payload.push_back(static_cast<std::byte>(0x80 | ((ch >> 6) & 0x3f)));
                str_payload.push_back(static_cast<std::byte>(0x80 | (ch & 0x3f)));
            }
        }
        str_payload.push_back(static_cast<std::byte>(0)); // NUL terminator
    }

    const uint32_t total_len = 16 + 2 + static_cast<uint32_t>(str_payload.size());
    out.reserve(total_len);
    write_le32(out, FUNCTIONFS_STRINGS_MAGIC);
    write_le32(out, total_len);
    write_le32(out, str_count);
    write_le32(out, 1); // lang_count
    write_le16(out, lang);
    for (std::byte b : str_payload) out.push_back(b);

    return Status::Ok;
}

[[nodiscard]] inline bool translate_functionfs_event(
    const usb_functionfs_event& ffs_ev, State& state, Event& out_event) {
    switch (ffs_ev.type) {
    case FUNCTIONFS_BIND:
        state = State::Default;
        out_event = Event{EventKind::None, {}};
        return false;
    case FUNCTIONFS_UNBIND:
        state = State::Detached;
        out_event = Event{EventKind::None, {}};
        return false;
    case FUNCTIONFS_ENABLE:
        state = State::Configured;
        out_event = Event{EventKind::Configured, {}};
        return true;
    case FUNCTIONFS_DISABLE:
        state = State::Default;
        out_event = Event{EventKind::Deconfigured, {}};
        return true;
    case FUNCTIONFS_SUSPEND:
        state = State::Suspended;
        out_event = Event{EventKind::Suspended, {}};
        return true;
    case FUNCTIONFS_RESUME:
        state = State::Configured;
        out_event = Event{EventKind::Resumed, {}};
        return true;
    case FUNCTIONFS_SETUP: {
        SetupPacket setup;
        setup.request_type = ffs_ev.u.setup.bRequestType;
        setup.request = ffs_ev.u.setup.bRequest;
        setup.value = static_cast<uint16_t>(ffs_ev.u.setup.wValue);
        setup.index = static_cast<uint16_t>(ffs_ev.u.setup.wIndex);
        setup.length = static_cast<uint16_t>(ffs_ev.u.setup.wLength);
        out_event = Event{EventKind::Setup, setup};
        return true;
    }
    default:
        out_event = Event{EventKind::None, {}};
        return false;
    }
}

[[nodiscard]] inline Status validate_gadget_ids(
    const std::filesystem::path& gadget_dir,
    std::span<const std::byte> device_desc) {
    if (device_desc.size() < 12) return Status::BadArgument;
    const auto vendor_path = gadget_dir / "idVendor";
    const auto product_path = gadget_dir / "idProduct";
    std::ifstream vf(vendor_path);
    std::ifstream pf(product_path);
    if (!vf.is_open() || !pf.is_open()) return Status::Ok;
    std::string vstr, pstr;
    std::getline(vf, vstr);
    std::getline(pf, pstr);
    uint16_t g_vendor = 0, g_product = 0;
    if (parse_configfs_id(vstr, g_vendor) && parse_configfs_id(pstr, g_product)) {
        const uint16_t d_vendor = static_cast<uint8_t>(device_desc[8]) |
                                 (static_cast<uint16_t>(static_cast<uint8_t>(device_desc[9])) << 8);
        const uint16_t d_product = static_cast<uint8_t>(device_desc[10]) |
                                  (static_cast<uint16_t>(static_cast<uint8_t>(device_desc[11])) << 8);
        if (g_vendor != d_vendor || g_product != d_product) {
            return Status::BadArgument;
        }
    }
    return Status::Ok;
}

struct DeviceTestHooks {
    std::string functionfs;
    std::string gadget;
};

inline const DeviceTestHooks* active_test_hooks = nullptr;

inline void set_device_test_hooks(const DeviceTestHooks* hooks) {
    active_test_hooks = hooks;
}

class FunctionFsDevice final : public Device {
public:
    FunctionFsDevice() = default;
    ~FunctionFsDevice() override {
        cleanup();
    }

    [[nodiscard]] Status initialize(const Descriptors& descs) override {
        std::string ffs_path;
        std::string gadget_path;
        if (active_test_hooks != nullptr) {
            ffs_path = active_test_hooks->functionfs;
            gadget_path = active_test_hooks->gadget;
        } else {
            const auto& res = platform::linux::resolve();
            if (res.status == platform::linux::MapStatus::Ok && res.map != nullptr) {
                ffs_path = res.map->usb_device.functionfs;
                gadget_path = res.map->usb_device.gadget;
            }
        }
        if (ffs_path.empty()) {
            return Status::Unsupported;
        }

        if (descs.device.size() < 12) return Status::BadArgument;

        // Verify gadget vendor and product match if gadget directory is configured and readable
        if (!gadget_path.empty()) {
            const Status s_id = validate_gadget_ids(gadget_path, descs.device);
            if (s_id != Status::Ok) return s_id;
        }

        std::vector<std::byte> desc_block;
        const auto s_desc = build_functionfs_descriptors(descs, desc_block);
        if (s_desc != Status::Ok) return s_desc;

        std::vector<std::byte> strings_block;
        const auto s_str = build_functionfs_strings(descs, strings_block);
        if (s_str != Status::Ok) return s_str;

        cleanup();

        const auto ep0_path = std::filesystem::path(ffs_path) / "ep0";
        ep0_fd_ = ::open(ep0_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (ep0_fd_ < 0) {
            return Status::TransportError;
        }

        const ssize_t w_desc = ::write(ep0_fd_, desc_block.data(), desc_block.size());
        if (w_desc < 0 || static_cast<size_t>(w_desc) != desc_block.size()) {
            cleanup();
            return Status::TransportError;
        }

        const ssize_t w_str = ::write(ep0_fd_, strings_block.data(), strings_block.size());
        if (w_str < 0 || static_cast<size_t>(w_str) != strings_block.size()) {
            cleanup();
            return Status::TransportError;
        }

        // Open ep1 to epN in endpoint descriptor order
        std::span<const std::byte> sub = descs.configuration;
        if (sub.size() >= 2 && static_cast<uint8_t>(sub[1]) == 2) {
            sub = sub.subspan(static_cast<uint8_t>(sub[0]));
        }

        size_t offset = 0;
        unsigned int ep_idx = 1;
        while (offset < sub.size()) {
            const uint8_t len = static_cast<uint8_t>(sub[offset]);
            const uint8_t type = static_cast<uint8_t>(sub[offset + 1]);
            if (type == 5 /* USB_DT_ENDPOINT */ && len >= 7) {
                const uint8_t addr_byte = static_cast<uint8_t>(sub[offset + 2]);
                const uint16_t max_pkt = static_cast<uint8_t>(sub[offset + 4]) |
                                         (static_cast<uint16_t>(static_cast<uint8_t>(sub[offset + 5])) << 8);

                const auto ep_path = std::filesystem::path(ffs_path) /
                                     ("ep" + std::to_string(ep_idx));
                const int ep_fd = ::open(ep_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
                if (ep_fd < 0) {
                    cleanup();
                    return Status::TransportError;
                }

                EndpointInfo info;
                info.index = static_cast<unsigned int>(endpoints_.size());
                info.address = EndpointAddress{addr_byte};
                info.fd = ep_fd;
                info.max_packet_size = max_pkt > 0 ? max_pkt : 64;
                info.write_buf.resize(info.max_packet_size > 512 ? info.max_packet_size : 4096);
                info.read_buf.resize(info.max_packet_size > 512 ? info.max_packet_size : 4096);
                endpoints_.push_back(std::move(info));
                ++ep_idx;
            }
            offset += len;
        }

        if (aio_setup(64, &aio_ctx_) < 0) {
            cleanup();
            return Status::TransportError;
        }

        eventfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (eventfd_ < 0) {
            cleanup();
            return Status::TransportError;
        }

        state_ = State::Default;
        return Status::Ok;
    }

    [[nodiscard]] Status attach() override {
        const auto& res = platform::linux::resolve();
        if (res.status != platform::linux::MapStatus::Ok || res.map == nullptr) {
            return Status::Unsupported;
        }
        const auto& map = *res.map;
        if (map.usb_device.functionfs.empty() || map.usb_device.gadget.empty()) {
            return Status::Unsupported;
        }

        std::string udc_name;
        if (map.usb_device.udc.kind == SelectorKind::Name) {
            udc_name = map.usb_device.udc.name;
        } else if (map.usb_device.udc.kind == SelectorKind::Auto) {
            const std::filesystem::path udc_dir = "/sys/class/udc";
            std::error_code ec;
            if (std::filesystem::exists(udc_dir, ec) && std::filesystem::is_directory(udc_dir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(udc_dir, ec)) {
                    if (entry.is_directory(ec) || entry.is_symlink(ec)) {
                        udc_name = entry.path().filename().string();
                        break;
                    }
                }
            }
            if (udc_name.empty()) {
                return Status::TransportError;
            }
        } else {
            return Status::Unsupported;
        }

        if (udc_name.empty()) return Status::Unsupported;

        const auto udc_file = std::filesystem::path(map.usb_device.gadget) / "UDC";
        const int ufd = ::open(udc_file.c_str(), O_WRONLY | O_CLOEXEC);
        if (ufd < 0) return Status::TransportError;
        const std::string to_write = udc_name + "\n";
        const ssize_t w = ::write(ufd, to_write.c_str(), to_write.size());
        ::close(ufd);
        if (w < 0) {
            return errno == EBUSY ? Status::Busy : Status::TransportError;
        }
        return Status::Ok;
    }

    [[nodiscard]] Status detach() override {
        const auto& res = platform::linux::resolve();
        if (res.status != platform::linux::MapStatus::Ok || res.map == nullptr) {
            return Status::Unsupported;
        }
        const auto& map = *res.map;
        if (map.usb_device.functionfs.empty() || map.usb_device.gadget.empty()) {
            return Status::Unsupported;
        }
        if (map.usb_device.udc.kind != SelectorKind::Auto &&
            map.usb_device.udc.kind != SelectorKind::Name) {
            return Status::Unsupported;
        }

        const auto udc_file = std::filesystem::path(map.usb_device.gadget) / "UDC";
        const int ufd = ::open(udc_file.c_str(), O_WRONLY | O_CLOEXEC);
        if (ufd < 0) return Status::TransportError;
        const ssize_t w = ::write(ufd, "\n", 1);
        ::close(ufd);
        if (w < 0) return Status::TransportError;
        state_ = State::Detached;
        return Status::Ok;
    }

    [[nodiscard]] State state() const override {
        if (!has_functionfs_configured()) return State::Detached;
        return state_;
    }

    [[nodiscard]] Speed speed() const override {
        if (!has_functionfs_configured() || state_ != State::Configured) return Speed::Unknown;
        return speed_;
    }

    [[nodiscard]] Status take_event(Event& event) override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0) return Status::NotInitialized;

        reap_completions();

        usb_functionfs_event events[8];
        const ssize_t n = ::read(ep0_fd_, events, sizeof(events));
        if (n > 0) {
            const size_t count = static_cast<size_t>(n) / sizeof(usb_functionfs_event);
            for (size_t i = 0; i < count; ++i) {
                Event ev;
                if (translate_functionfs_event(events[i], state_, ev)) {
                    if (ev.kind == EventKind::Configured) {
                        query_speed();
                        prime_out_endpoints();
                    }
                    if (event_queue_.size() >= 32) {
                        event_queue_.pop_front();
                        overflow_latched_ = true;
                    }
                    event_queue_.push_back(ev);
                }
            }
        }

        if (overflow_latched_) {
            overflow_latched_ = false;
            return Status::TransportError;
        }

        if (!event_queue_.empty()) {
            event = event_queue_.front();
            event_queue_.pop_front();
            if (event.kind == EventKind::Setup) {
                last_setup_ = event.setup;
                setup_pending_ = true;
            }
            return Status::Ok;
        }

        event = Event{EventKind::None, {}};
        return Status::Ok;
    }

    [[nodiscard]] Status control_receive(std::span<std::byte> buf, std::size_t& transferred) override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0) return Status::NotInitialized;
        if (!setup_pending_ || last_setup_.direction() != Direction::Out) {
            return Status::BadArgument;
        }

        const size_t to_read = std::min(buf.size(), static_cast<size_t>(last_setup_.length));
        const ssize_t ret = ::read(ep0_fd_, buf.data(), to_read);
        if (ret < 0) {
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? Status::Busy : Status::TransportError;
        }
        transferred = static_cast<size_t>(ret);
        return Status::Ok;
    }

    [[nodiscard]] Status control_reply(std::span<const std::byte> data) override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0) return Status::NotInitialized;
        if (!setup_pending_ || data.size() > last_setup_.length) {
            return Status::BadArgument;
        }

        const ssize_t ret = ::write(ep0_fd_, data.data(), data.size());
        setup_pending_ = false;
        if (ret < 0) {
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? Status::Busy : Status::TransportError;
        }
        return Status::Ok;
    }

    [[nodiscard]] Status control_stall() override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0) return Status::NotInitialized;
        if (!setup_pending_) return Status::BadArgument;

        // Read or write in the wrong direction to stall ep0
        if (last_setup_.direction() == Direction::In) {
            char dummy = 0;
            (void)::read(ep0_fd_, &dummy, 0);
        } else {
            if (last_setup_.length == 0) {
                char dummy = 0;
                (void)::read(ep0_fd_, &dummy, 0);
            } else {
                char dummy = 0;
                (void)::write(ep0_fd_, &dummy, 0);
            }
        }
        setup_pending_ = false;
        return Status::Ok;
    }

    [[nodiscard]] Status write(EndpointAddress ep, std::span<const std::byte> data,
                               std::size_t& accepted) override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0 || state_ != State::Configured) return Status::NotInitialized;
        if (!ep.in()) return Status::BadArgument;

        auto* endpoint = find_endpoint(ep);
        if (endpoint == nullptr) return Status::BadArgument;

        reap_completions();

        if (endpoint->write_in_flight) {
            accepted = 0;
            return Status::Ok;
        }

        const size_t to_write = std::min(data.size(), endpoint->write_buf.size());
        if (to_write > 0) {
            std::memcpy(endpoint->write_buf.data(), data.data(), to_write);
        }

        endpoint->write_iocb = {};
        endpoint->write_iocb.aio_fildes = endpoint->fd;
        endpoint->write_iocb.aio_lio_opcode = IOCB_CMD_PWRITE;
        endpoint->write_iocb.aio_buf = reinterpret_cast<uint64_t>(endpoint->write_buf.data());
        endpoint->write_iocb.aio_nbytes = to_write;
        endpoint->write_iocb.aio_offset = 0;
        endpoint->write_iocb.aio_flags = IOCB_FLAG_RESFD;
        endpoint->write_iocb.aio_resfd = eventfd_;
        endpoint->write_iocb.aio_data = endpoint->index;

        struct iocb* cbs[1] = { &endpoint->write_iocb };
        if (aio_submit(aio_ctx_, 1, cbs) != 1) {
            return Status::TransportError;
        }

        endpoint->write_in_flight = true;
        accepted = to_write;
        return Status::Ok;
    }

    [[nodiscard]] Status read(EndpointAddress ep, std::span<std::byte> buf,
                              std::size_t& received) override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0 || state_ != State::Configured) return Status::NotInitialized;
        if (ep.in()) return Status::BadArgument;

        auto* endpoint = find_endpoint(ep);
        if (endpoint == nullptr) return Status::BadArgument;

        reap_completions();

        if (endpoint->read_available == 0) {
            if (!endpoint->read_in_flight) {
                prime_read(*endpoint);
            }
            received = 0;
            return Status::Ok;
        }

        const size_t to_copy = std::min(buf.size(), endpoint->read_available);
        std::memcpy(buf.data(), endpoint->read_buf.data() + endpoint->read_offset, to_copy);
        endpoint->read_offset += to_copy;
        endpoint->read_available -= to_copy;
        received = to_copy;

        if (endpoint->read_available == 0) {
            endpoint->read_offset = 0;
            prime_read(*endpoint);
        }

        return Status::Ok;
    }

    [[nodiscard]] Status stall(EndpointAddress ep, bool halt) override {
        if (!has_functionfs_configured()) return Status::Unsupported;
        if (ep0_fd_ < 0 || state_ != State::Configured) return Status::NotInitialized;

        auto* endpoint = find_endpoint(ep);
        if (endpoint == nullptr) return Status::BadArgument;

        if (!halt) {
            if (::ioctl(endpoint->fd, FUNCTIONFS_CLEAR_HALT) < 0) {
                return Status::TransportError;
            }
            return Status::Ok;
        }

        // Set stall via wrong-direction transfer
        if (ep.in()) {
            char dummy = 0;
            (void)::read(endpoint->fd, &dummy, 0);
        } else {
            char dummy = 0;
            (void)::write(endpoint->fd, &dummy, 0);
        }
        return Status::Ok;
    }

private:
    struct EndpointInfo {
        unsigned int index = 0;
        EndpointAddress address;
        int fd = -1;
        uint16_t max_packet_size = 64;
        std::vector<std::byte> write_buf;
        bool write_in_flight = false;
        struct iocb write_iocb = {};

        std::vector<std::byte> read_buf;
        bool read_in_flight = false;
        size_t read_available = 0;
        size_t read_offset = 0;
        struct iocb read_iocb = {};
    };

    static bool has_functionfs_configured() {
        if (active_test_hooks != nullptr && !active_test_hooks->functionfs.empty()) {
            return true;
        }
        const auto& res = platform::linux::resolve();
        return res.status == platform::linux::MapStatus::Ok && res.map != nullptr &&
               !res.map->usb_device.functionfs.empty();
    }

    void cleanup() {
        for (auto& ep : endpoints_) {
            if (ep.fd >= 0) {
                ::close(ep.fd);
                ep.fd = -1;
            }
        }
        endpoints_.clear();
        if (aio_ctx_ != 0) {
            aio_destroy(aio_ctx_);
            aio_ctx_ = 0;
        }
        if (eventfd_ >= 0) {
            ::close(eventfd_);
            eventfd_ = -1;
        }
        if (ep0_fd_ >= 0) {
            ::close(ep0_fd_);
            ep0_fd_ = -1;
        }
        state_ = State::Detached;
        speed_ = Speed::Unknown;
        event_queue_.clear();
        overflow_latched_ = false;
        setup_pending_ = false;
    }

    [[nodiscard]] EndpointInfo* find_endpoint(EndpointAddress ep) {
        for (auto& info : endpoints_) {
            if (info.address == ep) return &info;
        }
        return nullptr;
    }

    void reap_completions() {
        if (aio_ctx_ == 0) return;
        if (eventfd_ >= 0) {
            uint64_t val = 0;
            (void)::read(eventfd_, &val, sizeof(val));
        }
        struct io_event aio_events[32];
        struct timespec ts = {0, 0};
        const int r = aio_getevents(aio_ctx_, 0, 32, aio_events, &ts);
        if (r > 0) {
            for (int i = 0; i < r; ++i) {
                const uint64_t idx = aio_events[i].data;
                if (idx < endpoints_.size()) {
                    auto& ep = endpoints_[idx];
                    if (ep.address.in()) {
                        ep.write_in_flight = false;
                    } else {
                        ep.read_in_flight = false;
                        if (aio_events[i].res >= 0) {
                            ep.read_available = static_cast<size_t>(aio_events[i].res);
                            ep.read_offset = 0;
                        } else {
                            ep.read_available = 0;
                        }
                    }
                }
            }
        }
    }

    void prime_read(EndpointInfo& ep) {
        if (ep.read_in_flight || aio_ctx_ == 0 || ep.fd < 0) return;
        ep.read_iocb = {};
        ep.read_iocb.aio_fildes = ep.fd;
        ep.read_iocb.aio_lio_opcode = IOCB_CMD_PREAD;
        ep.read_iocb.aio_buf = reinterpret_cast<uint64_t>(ep.read_buf.data());
        ep.read_iocb.aio_nbytes = ep.read_buf.size();
        ep.read_iocb.aio_offset = 0;
        ep.read_iocb.aio_flags = IOCB_FLAG_RESFD;
        ep.read_iocb.aio_resfd = eventfd_;
        ep.read_iocb.aio_data = ep.index;

        struct iocb* cbs[1] = { &ep.read_iocb };
        if (aio_submit(aio_ctx_, 1, cbs) == 1) {
            ep.read_in_flight = true;
        }
    }

    void prime_out_endpoints() {
        for (auto& ep : endpoints_) {
            if (!ep.address.in()) {
                prime_read(ep);
            }
        }
    }

    void query_speed() {
        speed_ = Speed::Full;
        if (!endpoints_.empty() && endpoints_.front().fd >= 0) {
            struct usb_endpoint_descriptor desc = {};
            if (::ioctl(endpoints_.front().fd, FUNCTIONFS_ENDPOINT_DESC, &desc) >= 0) {
                const uint16_t max_pkt = desc.wMaxPacketSize;
                if (max_pkt == 512) speed_ = Speed::High;
                else if (max_pkt <= 64) speed_ = Speed::Full;
            }
        }
    }

    int ep0_fd_ = -1;
    int eventfd_ = -1;
    aio_context_t aio_ctx_ = 0;
    State state_ = State::Detached;
    Speed speed_ = Speed::Unknown;
    std::vector<EndpointInfo> endpoints_;
    std::deque<Event> event_queue_;
    bool overflow_latched_ = false;
    bool setup_pending_ = false;
    SetupPacket last_setup_;
};

inline FunctionFsDevice provider_instance;

struct ProviderRegister {
    ProviderRegister() {
        mm::usb::device::set_device(provider_instance);
    }
};

inline const ProviderRegister registered;

}  // namespace platform::linux::usb::device
