// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <libusb-1.0/libusb.h>
#include <filesystem>
#include <fstream>
#include <string>

import mm.usb;
import mm.usb.host;
import mm.test;
import platform.linux.map;
import platform.linux.defaults;
import platform.linux.usb.host;

namespace {

using mm::test::expect;
using mm::usb::Status;
using namespace platform::linux::usb::host;

void libusb_error_mapping() {
    expect(map_libusb_error(LIBUSB_SUCCESS) == Status::Ok, "LIBUSB_SUCCESS is Ok");
    expect(map_libusb_error(LIBUSB_ERROR_IO) == Status::TransportError,
           "LIBUSB_ERROR_IO is TransportError");
    expect(map_libusb_error(LIBUSB_ERROR_INVALID_PARAM) == Status::BadArgument,
           "LIBUSB_ERROR_INVALID_PARAM is BadArgument");
    expect(map_libusb_error(LIBUSB_ERROR_ACCESS) == Status::TransportError,
           "LIBUSB_ERROR_ACCESS is TransportError");
    expect(map_libusb_error(LIBUSB_ERROR_NO_DEVICE) == Status::TransportError,
           "LIBUSB_ERROR_NO_DEVICE is TransportError");
    expect(map_libusb_error(LIBUSB_ERROR_NOT_FOUND) == Status::BadArgument,
           "LIBUSB_ERROR_NOT_FOUND is BadArgument");
    expect(map_libusb_error(LIBUSB_ERROR_BUSY) == Status::Busy,
           "LIBUSB_ERROR_BUSY is Busy");
    expect(map_libusb_error(LIBUSB_ERROR_TIMEOUT) == Status::Timeout,
           "LIBUSB_ERROR_TIMEOUT is Timeout");
    expect(map_libusb_error(LIBUSB_ERROR_OVERFLOW) == Status::TransportError,
           "LIBUSB_ERROR_OVERFLOW is TransportError");
    expect(map_libusb_error(LIBUSB_ERROR_PIPE) == Status::TransportError,
           "LIBUSB_ERROR_PIPE is TransportError");
    expect(map_libusb_error(LIBUSB_ERROR_INTERRUPTED) == Status::Busy,
           "LIBUSB_ERROR_INTERRUPTED is Busy");
    expect(map_libusb_error(LIBUSB_ERROR_NO_MEM) == Status::TransportError,
           "LIBUSB_ERROR_NO_MEM is TransportError");
    expect(map_libusb_error(LIBUSB_ERROR_NOT_SUPPORTED) == Status::Unsupported,
           "LIBUSB_ERROR_NOT_SUPPORTED is Unsupported");
    expect(map_libusb_error(-999) == Status::TransportError,
           "unrecognised libusb error maps to TransportError");
}

void forbidden_drivers_refusal() {
    expect(is_forbidden_driver("usb-storage"), "usb-storage is forbidden");
    expect(is_forbidden_driver("uas"), "uas is forbidden");
    expect(is_forbidden_driver("usbhid"), "usbhid is forbidden");

    expect(!is_forbidden_driver("cdc_acm"), "cdc_acm is permitted");
    expect(!is_forbidden_driver("ftdi_sio"), "ftdi_sio is permitted");
    expect(!is_forbidden_driver(""), "empty driver is not forbidden");
}

void sysfs_driver_inspection() {
    const std::filesystem::path fixture_dir = "usb_sysfs_fixture";
    std::filesystem::remove_all(fixture_dir);
    std::filesystem::create_directories(fixture_dir / "drivers" / "usb-storage");
    std::filesystem::create_directories(fixture_dir / "drivers" / "uas");
    std::filesystem::create_directories(fixture_dir / "drivers" / "usbhid");
    std::filesystem::create_directories(fixture_dir / "drivers" / "cdc_acm");

    const auto dev_dir = fixture_dir / "devices" / "1-2";
    std::filesystem::create_directories(dev_dir);

    {
        std::ofstream bus(dev_dir / "busnum");
        bus << "1\n";
        std::ofstream dev(dev_dir / "devnum");
        dev << "2\n";
    }

    // iface 0: usb-storage
    const auto iface0 = dev_dir / "1-2:1.0";
    std::filesystem::create_directories(iface0);
    {
        std::ofstream ifnum(iface0 / "bInterfaceNumber");
        ifnum << "0\n";
    }
    std::filesystem::create_directory_symlink("../../drivers/usb-storage", iface0 / "driver");

    // iface 1: uas
    const auto iface1 = dev_dir / "1-2:1.1";
    std::filesystem::create_directories(iface1);
    {
        std::ofstream ifnum(iface1 / "bInterfaceNumber");
        ifnum << "1\n";
    }
    std::filesystem::create_directory_symlink("../../drivers/uas", iface1 / "driver");

    // iface 2: usbhid
    const auto iface2 = dev_dir / "1-2:1.2";
    std::filesystem::create_directories(iface2);
    {
        std::ofstream ifnum(iface2 / "bInterfaceNumber");
        ifnum << "2\n";
    }
    std::filesystem::create_directory_symlink("../../drivers/usbhid", iface2 / "driver");

    // iface 3: cdc_acm
    const auto iface3 = dev_dir / "1-2:1.3";
    std::filesystem::create_directories(iface3);
    {
        std::ofstream ifnum(iface3 / "bInterfaceNumber");
        ifnum << "3\n";
    }
    std::filesystem::create_directory_symlink("../../drivers/cdc_acm", iface3 / "driver");

    const std::string sysfs_base = (fixture_dir / "devices").string();

    expect(inspect_interface_driver(1, 2, 0, sysfs_base) == "usb-storage",
           "inspects usb-storage driver");
    expect(inspect_interface_driver(1, 2, 1, sysfs_base) == "uas",
           "inspects uas driver");
    expect(inspect_interface_driver(1, 2, 2, sysfs_base) == "usbhid",
           "inspects usbhid driver");
    expect(inspect_interface_driver(1, 2, 3, sysfs_base) == "cdc_acm",
           "inspects cdc_acm driver");

    expect(inspect_interface_driver(1, 2, 99, sysfs_base).empty(),
           "unmatched interface reports empty driver");
    expect(inspect_interface_driver(99, 99, 0, sysfs_base).empty(),
           "unmatched device reports empty driver");

    std::filesystem::remove_all(fixture_dir);
}

void device_map_detach_kernel_drivers() {
    platform::linux::Map map;
    platform::linux::ParseError error;

    const mm::test::scoped_file file_yes{
        "mm_linux_usb_yes.mdy",
        "usb.host.detach-kernel-drivers = yes\n"
    };
    expect(platform::linux::apply_override(map, file_yes.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "usb.host.detach-kernel-drivers = yes parses");
    expect(map.usb_host.detach_kernel_drivers,
           "detach_kernel_drivers decoded as true");

    const mm::test::scoped_file file_no{
        "mm_linux_usb_no.mdy",
        "usb.host.detach-kernel-drivers = no\n"
    };
    expect(platform::linux::apply_override(map, file_no.path().string(), error) ==
               platform::linux::MapStatus::Ok,
           "usb.host.detach-kernel-drivers = no parses");
    expect(!map.usb_host.detach_kernel_drivers,
           "detach_kernel_drivers decoded as false");

    const mm::test::scoped_file file_bad{
        "mm_linux_usb_bad.mdy",
        "usb.host.detach-kernel-drivers = maybe\n"
    };
    expect(platform::linux::apply_override(map, file_bad.path().string(), error) ==
               platform::linux::MapStatus::SyntaxError,
           "invalid boolean value rejected as syntax error");
}

void live_device_listing() {
    auto& host = mm::usb::host::selected_host();
    expect(host.initialize() == Status::Ok, "libusb host initialize");

    std::array<DeviceInfo, 32> devs{};
    std::size_t count = 0;
    expect(host.list(devs, count) == Status::Ok, "libusb host list succeeds");
}

const mm::test::case_ cases[] = {
    {"libusb error mapping", &libusb_error_mapping},
    {"forbidden drivers refusal", &forbidden_drivers_refusal},
    {"sysfs driver inspection", &sysfs_driver_inspection},
    {"device map detach-kernel-drivers", &device_map_detach_kernel_drivers},
    {"live device listing", &live_device_listing},
};

const mm::test::registrar reg{"platform.linux.usb.host", cases};

}  // namespace
