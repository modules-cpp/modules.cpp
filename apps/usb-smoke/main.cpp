// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

import mm.usb;
import mm.usb.device;
import mm.usb.host;
import mm.usb.vendor;

using mm::usb::Status;

namespace {

int run_device_echo(unsigned int count_target) {
    auto& device = mm::usb::device::selected_device();
    const auto init_status = device.initialize(mm::usb::vendor::default_descriptors());
    if (init_status == Status::Unsupported) return 0;
    if (init_status != Status::Ok) return 1;

    mm::usb::vendor::DeviceEcho echo_service(device);
    const auto attach_status = echo_service.attach();
    if (attach_status != Status::Ok && attach_status != Status::Unsupported) return 1;

    // Run poll loop until target echos reached (or 100k cycles in smoke test)
    constexpr unsigned int max_polls = 100'000;
    unsigned int polls = 0;
    while (polls++ < max_polls) {
        const auto poll_status = echo_service.poll();
        if (poll_status != Status::Ok) return 1;

        if (count_target > 0 && echo_service.total_echoed() >= count_target) {
            break;
        }
    }

    (void)echo_service.detach();
    return 0;
}

int run_host_echo() {
    auto& host = mm::usb::host::selected_host();
    const auto init_status = host.initialize();
    if (init_status == Status::Unsupported) return 0;
    if (init_status != Status::Ok) return 2;

    mm::usb::vendor::HostEcho client(host);
    mm::usb::host::Handle handle;

    // Try finding the vendor echo device
    const auto find_status = client.find_and_open(handle);
    if (find_status == Status::Unsupported) return 0;
    if (find_status == Status::BadArgument) {
        // No vendor device present; exit cleanly for unattached smoke run
        return 0;
    }
    if (find_status != Status::Ok) return 3;

    // Perform bulk echo transfer
    std::array<std::byte, 64> tx_buffer{};
    for (std::size_t i = 0; i < tx_buffer.size(); ++i) {
        tx_buffer[i] = static_cast<std::byte>((i * 7 + 1) & 0xff);
    }
    std::array<std::byte, 64> rx_buffer{};
    std::size_t transferred = 0;

    const auto echo_status = client.echo(handle, tx_buffer, rx_buffer, transferred, 2000);
    if (echo_status != Status::Ok) {
        (void)client.close(handle);
        return 4;
    }

    // Perform vendor control request
    std::array<std::byte, 4> ctrl_buffer{};
    std::size_t ctrl_len = 0;
    const auto ctrl_status = client.vendor_control(
        handle, mm::usb::vendor::VENDOR_REQUEST_ECHO, 0x1234, ctrl_buffer, ctrl_len, 2000);
    (void)client.close(handle);

    if (ctrl_status != Status::Ok) return 5;
    if (ctrl_len < 2 || ctrl_buffer[0] != std::byte{0xaa} || ctrl_buffer[1] != std::byte{0x55}) {
        return 5;
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool mode_device = false;
    bool mode_host = false;
    unsigned int count = 0;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--device" || arg == "-d") {
            mode_device = true;
        } else if (arg == "--host" || arg == "-c") {
            mode_host = true;
        } else if (arg == "--count" && i + 1 < argc) {
            count = static_cast<unsigned int>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    if (mode_device) {
        return run_device_echo(count);
    }
    if (mode_host) {
        return run_host_echo();
    }

    // Default smoke test behavior:
    // Check both host and device. If both are unserved fallback, exit 0.
    // If device is supported and host is unsupported (e.g. embedded device port), run device echo.
    // Otherwise try host echo.
    auto& host = mm::usb::host::selected_host();
    const auto host_init = host.initialize();
    if (host_init == Status::Ok) {
        return run_host_echo();
    }

    auto& dev = mm::usb::device::selected_device();
    const auto dev_init = dev.initialize(mm::usb::vendor::default_descriptors());
    if (dev_init == Status::Ok) {
        return run_device_echo(count);
    }

    // Both unsupported or not configured
    return 0;
}
