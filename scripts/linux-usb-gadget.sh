#!/bin/sh
# scripts/linux-usb-gadget.sh
#
# Creates or destroys a Linux USB gadget with FunctionFS using configfs.
#
# Usage:
#   scripts/linux-usb-gadget.sh create [gadget_name] [mount_point] [udc]
#   scripts/linux-usb-gadget.sh destroy [gadget_name] [mount_point]
#   scripts/linux-usb-gadget.sh status [gadget_name] [mount_point]
set -eu

cmd="${1:-status}"
gadget_name="${2:-mm}"
mount_point="${3:-/dev/ffs-mm}"
udc_target="${4:-}"

gadget_dir="/sys/kernel/config/usb_gadget/${gadget_name}"

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "error: $cmd requires root privileges (run with sudo)" >&2
        exit 1
    fi
}

case "$cmd" in
    create|start)
        require_root
        echo "Creating USB gadget '${gadget_name}' with FunctionFS at '${mount_point}'..."

        # Ensure configfs is mounted
        if [ ! -d /sys/kernel/config ]; then
            mkdir -p /sys/kernel/config
        fi
        if ! mountpoint -q /sys/kernel/config 2>/dev/null; then
            mount -t configfs none /sys/kernel/config 2>/dev/null || true
        fi

        # Load libcomposite if modular
        modprobe libcomposite 2>/dev/null || true

        # Create gadget directory
        mkdir -p "${gadget_dir}"
        echo "0x1d50" > "${gadget_dir}/idVendor"
        echo "0x6150" > "${gadget_dir}/idProduct"
        echo "0x0200" > "${gadget_dir}/bcdUSB"
        echo "0x0100" > "${gadget_dir}/bcdDevice"

        # English strings
        mkdir -p "${gadget_dir}/strings/0x409"
        echo "32bitmicro" > "${gadget_dir}/strings/0x409/manufacturer"
        echo "Modules.cpp USB Device" > "${gadget_dir}/strings/0x409/product"
        echo "0001" > "${gadget_dir}/strings/0x409/serialnumber"

        # Configuration 1
        mkdir -p "${gadget_dir}/configs/c.1/strings/0x409"
        echo "Default Configuration" > "${gadget_dir}/configs/c.1/strings/0x409/configuration"
        echo "250" > "${gadget_dir}/configs/c.1/MaxPower"

        # FunctionFS function
        mkdir -p "${gadget_dir}/functions/ffs.${gadget_name}"

        # Link function to configuration
        if [ ! -e "${gadget_dir}/configs/c.1/ffs.${gadget_name}" ]; then
            ln -s "${gadget_dir}/functions/ffs.${gadget_name}" "${gadget_dir}/configs/c.1/"
        fi

        # Mount FunctionFS
        mkdir -p "${mount_point}"
        if ! mountpoint -q "${mount_point}" 2>/dev/null; then
            mount -t functionfs "${gadget_name}" "${mount_point}"
        fi

        # Optional UDC binding
        if [ -n "${udc_target}" ]; then
            if [ "${udc_target}" = "auto" ]; then
                udc_found=""
                if [ -d /sys/class/udc ]; then
                    for u in /sys/class/udc/*; do
                        if [ -e "$u" ]; then
                            udc_found="$(basename "$u")"
                            break
                        fi
                    done
                fi
                if [ -n "$udc_found" ]; then
                    echo "$udc_found" > "${gadget_dir}/UDC"
                    echo "Bound gadget to UDC: $udc_found"
                else
                    echo "warning: no UDC found in /sys/class/udc" >&2
                fi
            else
                echo "${udc_target}" > "${gadget_dir}/UDC"
                echo "Bound gadget to UDC: ${udc_target}"
            fi
        fi

        echo "Gadget '${gadget_name}' configured successfully."
        ;;

    destroy|stop)
        require_root
        echo "Destroying USB gadget '${gadget_name}'..."

        # Unbind UDC if bound
        if [ -f "${gadget_dir}/UDC" ]; then
            echo "" > "${gadget_dir}/UDC" 2>/dev/null || true
        fi

        # Unmount FunctionFS
        if mountpoint -q "${mount_point}" 2>/dev/null; then
            umount "${mount_point}" 2>/dev/null || true
        fi
        rmdir "${mount_point}" 2>/dev/null || true

        # Remove function symlink from configuration
        rm -f "${gadget_dir}/configs/c.1/ffs.${gadget_name}" 2>/dev/null || true

        # Remove configuration and function directories
        rmdir "${gadget_dir}/configs/c.1/strings/0x409" 2>/dev/null || true
        rmdir "${gadget_dir}/configs/c.1" 2>/dev/null || true
        rmdir "${gadget_dir}/functions/ffs.${gadget_name}" 2>/dev/null || true
        rmdir "${gadget_dir}/strings/0x409" 2>/dev/null || true
        rmdir "${gadget_dir}" 2>/dev/null || true

        echo "Gadget '${gadget_name}' destroyed."
        ;;

    status)
        echo "USB Gadget '${gadget_name}' status:"
        if [ -d "${gadget_dir}" ]; then
            echo "  Configfs: PRESENT (${gadget_dir})"
            if [ -f "${gadget_dir}/UDC" ]; then
                udc_cur="$(cat "${gadget_dir}/UDC")"
                if [ -n "$udc_cur" ]; then
                    echo "  UDC: BOUND ($udc_cur)"
                else
                    echo "  UDC: UNBOUND"
                fi
            fi
        else
            echo "  Configfs: NOT FOUND"
        fi
        if mountpoint -q "${mount_point}" 2>/dev/null; then
            echo "  FunctionFS mount: MOUNTED at ${mount_point}"
        else
            echo "  FunctionFS mount: NOT MOUNTED (${mount_point})"
        fi
        ;;

    *)
        echo "usage: $0 {create|destroy|status} [gadget_name] [mount_point] [udc]" >&2
        exit 1
        ;;
esac
