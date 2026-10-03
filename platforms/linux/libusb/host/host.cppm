// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <libusb-1.0/libusb.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module platform.linux.usb.host;

import mm.usb;
import mm.usb.host;
import platform.linux.map;

export namespace platform::linux::usb::host {

using mm::usb::EndpointAddress;
using mm::usb::SetupPacket;
using mm::usb::Speed;
using mm::usb::Status;
using mm::usb::host::DeviceInfo;
using mm::usb::host::Event;
using mm::usb::host::EventKind;
using mm::usb::host::Handle;
using mm::usb::host::Host;

[[nodiscard]] constexpr Status map_libusb_error(int error_code) {
    switch (error_code) {
    case LIBUSB_SUCCESS:
        return Status::Ok;
    case LIBUSB_ERROR_IO:
        return Status::TransportError;
    case LIBUSB_ERROR_INVALID_PARAM:
        return Status::BadArgument;
    case LIBUSB_ERROR_ACCESS:
        return Status::TransportError;
    case LIBUSB_ERROR_NO_DEVICE:
        return Status::TransportError;
    case LIBUSB_ERROR_NOT_FOUND:
        return Status::BadArgument;
    case LIBUSB_ERROR_BUSY:
        return Status::Busy;
    case LIBUSB_ERROR_TIMEOUT:
        return Status::Timeout;
    case LIBUSB_ERROR_OVERFLOW:
        return Status::TransportError;
    case LIBUSB_ERROR_PIPE:
        return Status::TransportError;
    case LIBUSB_ERROR_INTERRUPTED:
        return Status::Busy;
    case LIBUSB_ERROR_NO_MEM:
        return Status::TransportError;
    case LIBUSB_ERROR_NOT_SUPPORTED:
        return Status::Unsupported;
    default:
        return Status::TransportError;
    }
}

[[nodiscard]] constexpr Speed to_speed(int libusb_speed) {
    switch (libusb_speed) {
    case LIBUSB_SPEED_LOW:
        return Speed::Low;
    case LIBUSB_SPEED_FULL:
        return Speed::Full;
    case LIBUSB_SPEED_HIGH:
        return Speed::High;
    default:
        return Speed::Unknown;
    }
}

[[nodiscard]] inline bool is_forbidden_driver(std::string_view driver) {
    return driver == "usb-storage" || driver == "uas" || driver == "usbhid";
}

struct UsbHostTestHooks {
    std::string sysfs_usb_dir;
    std::optional<bool> allow_detach;
};

inline const UsbHostTestHooks* test_hooks = nullptr;

inline void set_usb_host_test_hooks(const UsbHostTestHooks* hooks) {
    test_hooks = hooks;
}

[[nodiscard]] inline std::string inspect_interface_driver(
    std::uint8_t bus, std::uint8_t address, unsigned int iface_num,
    const std::string& sysfs_base = "/sys/bus/usb/devices") {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(sysfs_base, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;

        // Check if busnum and devnum match
        std::ifstream bus_file(entry.path() / "busnum");
        std::ifstream dev_file(entry.path() / "devnum");
        if (!bus_file.is_open() || !dev_file.is_open()) continue;

        unsigned int b = 0;
        unsigned int d = 0;
        bus_file >> b;
        dev_file >> d;
        if (b != bus || d != address) continue;

        // Find interface subdirectories
        for (const auto& sub : std::filesystem::directory_iterator(entry.path(), ec)) {
            if (ec) break;
            if (!sub.is_directory()) continue;
            std::ifstream iface_file(sub.path() / "bInterfaceNumber");
            if (!iface_file.is_open()) continue;
            unsigned int num = 0;
            iface_file >> num;
            if (num != iface_num) continue;

            const auto driver_link = sub.path() / "driver";
            if (std::filesystem::is_symlink(driver_link, ec)) {
                auto target = std::filesystem::read_symlink(driver_link, ec);
                if (!ec) {
                    return target.filename().string();
                }
            }
        }
    }
    return {};
}

class LibusbHost final : public Host {
public:
    struct OpenDevice {
        unsigned int id = 0;
        libusb_device_handle* handle = nullptr;
        std::uint8_t bus = 0;
        std::uint8_t address = 0;
        std::vector<int> claimed_interfaces;
        std::vector<int> detached_drivers;
    };

    LibusbHost() = default;

    ~LibusbHost() override {
        cleanup();
    }

    [[nodiscard]] Status initialize() override {
        if (ctx_) return Status::Ok;
        int ret = libusb_init_context(&ctx_, nullptr, 0);
        if (ret != 0) return map_libusb_error(ret);

        has_hotplug_ = (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) != 0);
        if (has_hotplug_) {
            libusb_hotplug_register_callback(
                ctx_,
                static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                                  LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
                LIBUSB_HOTPLUG_NO_FLAGS,
                LIBUSB_HOTPLUG_MATCH_ANY,
                LIBUSB_HOTPLUG_MATCH_ANY,
                LIBUSB_HOTPLUG_MATCH_ANY,
                &hotplug_callback,
                this,
                &hotplug_handle_);
        }
        return Status::Ok;
    }

    [[nodiscard]] Status list(std::span<DeviceInfo> out, std::size_t& count) override {
        if (!ctx_) {
            Status s = initialize();
            if (s != Status::Ok) return s;
        }
        libusb_device** dev_list = nullptr;
        const ssize_t n = libusb_get_device_list(ctx_, &dev_list);
        if (n < 0) return map_libusb_error(static_cast<int>(n));

        std::size_t written = 0;
        for (ssize_t i = 0; i < n && written < out.size(); ++i) {
            libusb_device* dev = dev_list[i];
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(dev, &desc) == 0) {
                out[written++] = DeviceInfo{
                    .bus = static_cast<std::uint8_t>(libusb_get_bus_number(dev)),
                    .address = static_cast<std::uint8_t>(libusb_get_device_address(dev)),
                    .vendor = desc.idVendor,
                    .product = desc.idProduct,
                    .speed = to_speed(libusb_get_device_speed(dev))
                };
            }
        }
        libusb_free_device_list(dev_list, 1);
        count = written;
        return Status::Ok;
    }

    [[nodiscard]] Status take_event(Event& ev) override {
        if (!ctx_) {
            Status s = initialize();
            if (s != Status::Ok) return s;
        }

        // Service pending libusb events with zero timeout
        timeval tv{0, 0};
        libusb_handle_events_timeout_completed(ctx_, &tv, nullptr);

        if (!has_hotplug_) {
            poll_devices();
        }

        if (event_queue_.empty()) {
            ev = Event{.kind = EventKind::None};
            return Status::Ok;
        }

        ev = event_queue_.front();
        event_queue_.erase(event_queue_.begin());
        return Status::Ok;
    }

    [[nodiscard]] Status open(const DeviceInfo& dev, Handle& handle) override {
        if (!ctx_) {
            Status s = initialize();
            if (s != Status::Ok) return s;
        }
        libusb_device** dev_list = nullptr;
        const ssize_t n = libusb_get_device_list(ctx_, &dev_list);
        if (n < 0) return map_libusb_error(static_cast<int>(n));

        libusb_device* target_dev = nullptr;
        for (ssize_t i = 0; i < n; ++i) {
            libusb_device* d = dev_list[i];
            if (libusb_get_bus_number(d) == dev.bus &&
                libusb_get_device_address(d) == dev.address) {
                target_dev = d;
                break;
            }
        }
        if (!target_dev) {
            libusb_free_device_list(dev_list, 1);
            return Status::BadArgument;
        }

        libusb_device_handle* dev_handle = nullptr;
        int ret = libusb_open(target_dev, &dev_handle);
        libusb_free_device_list(dev_list, 1);
        if (ret != 0) return map_libusb_error(ret);

        unsigned int id = next_handle_id_++;
        open_devices_.push_back(OpenDevice{
            .id = id,
            .handle = dev_handle,
            .bus = dev.bus,
            .address = dev.address
        });
        handle = Handle{id};
        return Status::Ok;
    }

    [[nodiscard]] Status close(Handle handle) override {
        auto it = std::find_if(open_devices_.begin(), open_devices_.end(),
                               [handle](const OpenDevice& op) { return op.id == handle.value; });
        if (it == open_devices_.end()) return Status::BadArgument;

        for (int iface : it->claimed_interfaces) {
            libusb_release_interface(it->handle, iface);
        }
        for (int iface : it->detached_drivers) {
            libusb_attach_kernel_driver(it->handle, iface);
        }
        libusb_close(it->handle);
        open_devices_.erase(it);
        return Status::Ok;
    }

    [[nodiscard]] Status descriptors(Handle handle, std::span<std::byte> out,
                                     std::size_t& count) override {
        OpenDevice* op = find_open(handle);
        if (!op) return Status::BadArgument;

        libusb_device* dev = libusb_get_device(op->handle);
        libusb_config_descriptor* config = nullptr;
        int ret = libusb_get_active_config_descriptor(dev, &config);
        if (ret != 0) return map_libusb_error(ret);

        const std::size_t config_len = config->wTotalLength;
        const std::size_t total_len = 18 + config_len;
        if (out.size() < total_len) {
            libusb_free_config_descriptor(config);
            return Status::BadArgument;
        }

        unsigned char dev_bytes[18];
        ret = libusb_get_descriptor(op->handle, LIBUSB_DT_DEVICE, 0, dev_bytes, 18);
        if (ret < 0) {
            libusb_free_config_descriptor(config);
            return map_libusb_error(ret);
        }
        std::memcpy(out.data(), dev_bytes, 18);

        ret = libusb_get_descriptor(op->handle, LIBUSB_DT_CONFIG, config->bConfigurationValue,
                                    reinterpret_cast<unsigned char*>(out.data() + 18),
                                    static_cast<int>(config_len));
        libusb_free_config_descriptor(config);
        if (ret < 0) return map_libusb_error(ret);

        count = 18 + static_cast<std::size_t>(ret);
        return Status::Ok;
    }

    [[nodiscard]] Status claim(Handle handle, unsigned int iface) override {
        OpenDevice* op = find_open(handle);
        if (!op) return Status::BadArgument;

        auto claimed_it = std::find(op->claimed_interfaces.begin(),
                                    op->claimed_interfaces.end(), static_cast<int>(iface));
        if (claimed_it != op->claimed_interfaces.end()) {
            return Status::Ok;
        }

        int active = libusb_kernel_driver_active(op->handle, static_cast<int>(iface));
        if (active == 1) {
            const std::string sysfs_base = test_hooks && !test_hooks->sysfs_usb_dir.empty()
                                               ? test_hooks->sysfs_usb_dir
                                               : "/sys/bus/usb/devices";
            const std::string driver_name =
                inspect_interface_driver(op->bus, op->address, iface, sysfs_base);
            if (is_forbidden_driver(driver_name)) {
                return Status::Busy;
            }

            bool detach_allowed = false;
            if (test_hooks && test_hooks->allow_detach.has_value()) {
                detach_allowed = *test_hooks->allow_detach;
            } else {
                const auto& res = platform::linux::resolve();
                if (res.status == platform::linux::MapStatus::Ok && res.map != nullptr) {
                    detach_allowed = res.map->usb_host.detach_kernel_drivers;
                }
            }

            if (!detach_allowed) {
                return Status::Busy;
            }

            int detach_ret = libusb_detach_kernel_driver(op->handle, static_cast<int>(iface));
            if (detach_ret != 0 && detach_ret != LIBUSB_ERROR_NOT_FOUND) {
                return map_libusb_error(detach_ret);
            }
            op->detached_drivers.push_back(static_cast<int>(iface));
        }

        int ret = libusb_claim_interface(op->handle, static_cast<int>(iface));
        if (ret != 0) {
            return map_libusb_error(ret);
        }
        op->claimed_interfaces.push_back(static_cast<int>(iface));
        return Status::Ok;
    }

    [[nodiscard]] Status release(Handle handle, unsigned int iface) override {
        OpenDevice* op = find_open(handle);
        if (!op) return Status::BadArgument;

        auto it = std::find(op->claimed_interfaces.begin(), op->claimed_interfaces.end(),
                            static_cast<int>(iface));
        if (it == op->claimed_interfaces.end()) return Status::BadArgument;
        op->claimed_interfaces.erase(it);

        int ret = libusb_release_interface(op->handle, static_cast<int>(iface));

        auto det_it = std::find(op->detached_drivers.begin(), op->detached_drivers.end(),
                                static_cast<int>(iface));
        if (det_it != op->detached_drivers.end()) {
            libusb_attach_kernel_driver(op->handle, static_cast<int>(iface));
            op->detached_drivers.erase(det_it);
        }
        return map_libusb_error(ret);
    }

    [[nodiscard]] Status control(Handle handle, const SetupPacket& setup,
                                 std::span<std::byte> data,
                                 std::size_t& transferred,
                                 unsigned long timeout_ms) override {
        OpenDevice* op = find_open(handle);
        if (!op) return Status::BadArgument;

        int ret = libusb_control_transfer(
            op->handle,
            setup.request_type,
            setup.request,
            setup.value,
            setup.index,
            reinterpret_cast<unsigned char*>(data.data()),
            static_cast<uint16_t>(data.size()),
            static_cast<unsigned int>(timeout_ms));

        if (ret >= 0) {
            transferred = static_cast<std::size_t>(ret);
            return Status::Ok;
        }
        if (ret == LIBUSB_ERROR_TIMEOUT) {
            transferred = 0;
            return Status::Timeout;
        }
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(op->handle, 0);
            return Status::TransportError;
        }
        return map_libusb_error(ret);
    }

    [[nodiscard]] Status transfer_out(Handle handle, EndpointAddress ep,
                                      std::span<const std::byte> data,
                                      std::size_t& transferred,
                                      unsigned long timeout_ms) override {
        OpenDevice* op = find_open(handle);
        if (!op || !ep.valid() || ep.in()) return Status::BadArgument;

        uint8_t ep_type = LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK;
        libusb_device* dev = libusb_get_device(op->handle);
        libusb_config_descriptor* config = nullptr;
        if (libusb_get_active_config_descriptor(dev, &config) == 0) {
            for (int i = 0; i < config->bNumInterfaces; ++i) {
                for (int a = 0; a < config->interface[i].num_altsetting; ++a) {
                    const auto& alt = config->interface[i].altsetting[a];
                    for (int e = 0; e < alt.bNumEndpoints; ++e) {
                        if (alt.endpoint[e].bEndpointAddress == ep.value) {
                            ep_type = alt.endpoint[e].bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                        }
                    }
                }
            }
            libusb_free_config_descriptor(config);
        }

        int actual = 0;
        int ret = 0;
        if (ep_type == LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT) {
            ret = libusb_interrupt_transfer(
                op->handle,
                ep.value,
                const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data.data())),
                static_cast<int>(data.size()),
                &actual,
                static_cast<unsigned int>(timeout_ms));
        } else {
            ret = libusb_bulk_transfer(
                op->handle,
                ep.value,
                const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data.data())),
                static_cast<int>(data.size()),
                &actual,
                static_cast<unsigned int>(timeout_ms));
        }

        if (ret == 0) {
            transferred = static_cast<std::size_t>(actual);
            return Status::Ok;
        }
        if (ret == LIBUSB_ERROR_TIMEOUT) {
            transferred = static_cast<std::size_t>(actual);
            return Status::Timeout;
        }
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(op->handle, ep.value);
            return Status::TransportError;
        }
        return map_libusb_error(ret);
    }

    [[nodiscard]] Status transfer_in(Handle handle, EndpointAddress ep,
                                     std::span<std::byte> data,
                                     std::size_t& transferred,
                                     unsigned long timeout_ms) override {
        OpenDevice* op = find_open(handle);
        if (!op || !ep.valid() || !ep.in()) return Status::BadArgument;

        uint8_t ep_type = LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK;
        libusb_device* dev = libusb_get_device(op->handle);
        libusb_config_descriptor* config = nullptr;
        if (libusb_get_active_config_descriptor(dev, &config) == 0) {
            for (int i = 0; i < config->bNumInterfaces; ++i) {
                for (int a = 0; a < config->interface[i].num_altsetting; ++a) {
                    const auto& alt = config->interface[i].altsetting[a];
                    for (int e = 0; e < alt.bNumEndpoints; ++e) {
                        if (alt.endpoint[e].bEndpointAddress == ep.value) {
                            ep_type = alt.endpoint[e].bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                        }
                    }
                }
            }
            libusb_free_config_descriptor(config);
        }

        int actual = 0;
        int ret = 0;
        if (ep_type == LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT) {
            ret = libusb_interrupt_transfer(
                op->handle,
                ep.value,
                reinterpret_cast<unsigned char*>(data.data()),
                static_cast<int>(data.size()),
                &actual,
                static_cast<unsigned int>(timeout_ms));
        } else {
            ret = libusb_bulk_transfer(
                op->handle,
                ep.value,
                reinterpret_cast<unsigned char*>(data.data()),
                static_cast<int>(data.size()),
                &actual,
                static_cast<unsigned int>(timeout_ms));
        }

        if (ret == 0) {
            transferred = static_cast<std::size_t>(actual);
            return Status::Ok;
        }
        if (ret == LIBUSB_ERROR_TIMEOUT) {
            transferred = static_cast<std::size_t>(actual);
            return Status::Timeout;
        }
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(op->handle, ep.value);
            return Status::TransportError;
        }
        return map_libusb_error(ret);
    }

private:
    void cleanup() {
        for (auto& op : open_devices_) {
            if (op.handle) {
                for (int iface : op.claimed_interfaces) {
                    libusb_release_interface(op.handle, iface);
                }
                for (int iface : op.detached_drivers) {
                    libusb_attach_kernel_driver(op.handle, iface);
                }
                libusb_close(op.handle);
            }
        }
        open_devices_.clear();

        if (ctx_) {
            if (has_hotplug_ && hotplug_handle_) {
                libusb_hotplug_deregister_callback(ctx_, hotplug_handle_);
                hotplug_handle_ = 0;
            }
            libusb_exit(ctx_);
            ctx_ = nullptr;
        }
    }

    OpenDevice* find_open(Handle h) {
        auto it = std::find_if(open_devices_.begin(), open_devices_.end(),
                               [h](const OpenDevice& op) { return op.id == h.value; });
        return it != open_devices_.end() ? &(*it) : nullptr;
    }

    static int LIBUSB_CALL hotplug_callback(libusb_context*, libusb_device* device,
                                            libusb_hotplug_event event, void* user_data) {
        auto* self = static_cast<LibusbHost*>(user_data);
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device, &desc) == 0) {
            DeviceInfo info{
                .bus = static_cast<std::uint8_t>(libusb_get_bus_number(device)),
                .address = static_cast<std::uint8_t>(libusb_get_device_address(device)),
                .vendor = desc.idVendor,
                .product = desc.idProduct,
                .speed = to_speed(libusb_get_device_speed(device))
            };
            if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
                self->event_queue_.push_back(Event{.kind = EventKind::Attached, .device = info});
            } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
                self->event_queue_.push_back(Event{.kind = EventKind::Detached, .device = info});
            }
        }
        return 0;
    }

    void poll_devices() {
        libusb_device** list = nullptr;
        const ssize_t n = libusb_get_device_list(ctx_, &list);
        if (n < 0) return;

        std::vector<DeviceInfo> current;
        current.reserve(n);
        for (ssize_t i = 0; i < n; ++i) {
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(list[i], &desc) == 0) {
                current.push_back(DeviceInfo{
                    .bus = static_cast<std::uint8_t>(libusb_get_bus_number(list[i])),
                    .address = static_cast<std::uint8_t>(libusb_get_device_address(list[i])),
                    .vendor = desc.idVendor,
                    .product = desc.idProduct,
                    .speed = to_speed(libusb_get_device_speed(list[i]))
                });
            }
        }
        libusb_free_device_list(list, 1);

        // Check for attached devices
        for (const auto& dev : current) {
            if (std::find(previous_devices_.begin(), previous_devices_.end(), dev) ==
                previous_devices_.end()) {
                event_queue_.push_back(Event{.kind = EventKind::Attached, .device = dev});
            }
        }
        // Check for detached devices
        for (const auto& dev : previous_devices_) {
            if (std::find(current.begin(), current.end(), dev) == current.end()) {
                event_queue_.push_back(Event{.kind = EventKind::Detached, .device = dev});
            }
        }
        previous_devices_ = std::move(current);
    }

    libusb_context* ctx_ = nullptr;
    bool has_hotplug_ = false;
    libusb_hotplug_callback_handle hotplug_handle_ = 0;
    std::vector<Event> event_queue_;
    std::vector<DeviceInfo> previous_devices_;
    std::vector<OpenDevice> open_devices_;
    unsigned int next_handle_id_ = 1;
};

inline LibusbHost provider_instance;

struct ProviderRegister {
    ProviderRegister() {
        mm::usb::host::set_host(provider_instance);
    }
};

inline const ProviderRegister registered;

}  // namespace platform::linux::usb::host
