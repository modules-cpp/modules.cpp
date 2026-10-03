#!/bin/sh
# Provisions the pinned Pico-PIO-USB checkout the pico-sdk bridge builds for a
# board with a PIO USB host port (MM_BOARD_HAS_USB_HOST in
# cmake/resolve-board.cmake).
#
# The companion of ../vendor.sh, and like it never invoked by a tool:
# provisioning is a deliberate act, and a build only ever reports that the
# checkout is absent. MM_PIO_USB_SOURCE names a local clone to copy from
# instead of the network, for a machine that already has one.
#
# Re-running it is safe. With the checkout already at the pinned commit it does
# nothing and exits zero.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

MM_COMMIT="5a37a66dc5d3fbe0ef3cdbeda923a757440f984f"
MM_URL="https://github.com/sekigon-gonnoc/Pico-PIO-USB.git"

MM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_SOURCE="${MM_DIR}/upstream"

# As in ../vendor.sh: the toplevel is checked before the commit, because an
# empty upstream/ would otherwise answer for this project's repository.
mm_checked_out() {
    [ -e "${MM_SOURCE}/.git" ] || return 1
    mm_top=$(git -C "${MM_SOURCE}" rev-parse --show-toplevel 2>/dev/null) || return 1
    [ "${mm_top}" = "${MM_SOURCE}" ] || return 1
    mm_head=$(git -C "${MM_SOURCE}" rev-parse HEAD 2>/dev/null) || return 1
    [ "${mm_head}" = "${MM_COMMIT}" ] || return 1
    [ -z "$(git -C "${MM_SOURCE}" status --porcelain=v1 --untracked-files=all)" ] || return 1
}

if mm_checked_out; then
    echo "Pico-PIO-USB already at ${MM_COMMIT}"
    exit 0
fi

if [ -e "${MM_SOURCE}/.git" ]; then
    echo "vendor.sh: ${MM_SOURCE} is not a clean checkout of ${MM_COMMIT}" >&2
    echo "vendor.sh: remove it and re-run to reprovision" >&2
    exit 65
fi
if [ -d "${MM_SOURCE}" ] && [ -n "$(ls -A "${MM_SOURCE}" 2>/dev/null)" ]; then
    echo "vendor.sh: ${MM_SOURCE} exists and is not a checkout; remove it first" >&2
    exit 65
fi
rmdir "${MM_SOURCE}" 2>/dev/null || true

mm_from=${MM_PIO_USB_SOURCE:-${MM_URL}}
echo "Cloning Pico-PIO-USB from ${mm_from} into ${MM_SOURCE}"
git clone --no-checkout "${mm_from}" "${MM_SOURCE}"
git -C "${MM_SOURCE}" checkout --quiet --detach "${MM_COMMIT}"

if ! mm_checked_out; then
    mm_head=$(git -C "${MM_SOURCE}" rev-parse HEAD 2>/dev/null || echo unknown)
    echo "vendor.sh: checkout is at ${mm_head}, expected ${MM_COMMIT}" >&2
    exit 65
fi
echo "Pico-PIO-USB checked out at ${MM_COMMIT}"
