#!/bin/sh
# scripts/qualify-linux-usb.sh
#
# Qualifies the Linux USB stack:
# 1. Verifies the libusb-1.0 library dependency and platform.linux.usb.host provider.
# 2. Lists USB devices through platform.linux.usb.host via tests/mm/linux.
# 3. Reports status of dummy_hcd module for local device-to-host loopback qualification.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=qualify-linux-usb
. "$(dirname -- "$0")/lib/common.sh"
mm_enter_root

case " $* " in
    *" -h "*|*" --help "*)
        echo "usage: qualify-linux-usb.sh"
        echo "  Qualifies platform.linux.usb.host by listing USB devices,"
        echo "  and checks for dummy_hcd kernel module availability."
        exit 0
        ;;
esac

echo "=== Linux USB Host & Device Qualification ==="

# Check libusb-1.0 development files
if ! pkg-config --exists libusb-1.0; then
    echo "$test_name: libusb-1.0 package not found via pkg-config" >&2
    exit 65
fi
libusb_ver=$(pkg-config --modversion libusb-1.0)
echo "Found libusb-1.0 version: $libusb_ver"

# Ensure host test binary exists or is runnable
if [ ! -x "out/bin/test" ]; then
    echo "$test_name: out/bin/test not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

# Run the linux platform USB host and device tests (including live device listing)
echo "Running platform.linux.usb.host and platform.linux.usb.device verification..."
out/bin/test tests/mm/linux/

echo "USB host and device platform tests: SUCCESS"

# Check gadget script and status
echo
echo "Checking Linux USB gadget script..."
if [ -x "scripts/linux-usb-gadget.sh" ]; then
    echo "scripts/linux-usb-gadget.sh: PRESENT and EXECUTABLE"
    ./scripts/linux-usb-gadget.sh status || true
fi

# Check for dummy_hcd module (needed for gadget loopback qualification in later stages)
echo
echo "Checking kernel dummy_hcd module status..."
if lsmod | grep -q "^dummy_hcd"; then
    echo "dummy_hcd: LOADED (ready for device-to-host loopback qualification)"
else
    echo "dummy_hcd: NOT LOADED"
    echo "  (Note: local loopback testing requires 'sudo modprobe dummy_hcd')"
fi

echo
echo "PASS: $test_name"
