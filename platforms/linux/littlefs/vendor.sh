#!/bin/sh
# Provisions the pinned littlefs checkout the linux-littlefs library compiles
# for a program that links platform.linux.fs.littlefs, the provider of
# mm.fs.littlefs on the Linux SDKs. Linux's own copy: the Pico SDK bridge
# compiles another, platforms/pico/sdk/pico-sdk/littlefs, pinned on its own.
#
# Never invoked by a tool: provisioning is a deliberate act, and a build only
# ever reports that the checkout is absent, naming this script. MM_LITTLEFS_SOURCE names a local clone to copy from
# instead of the network, for a machine that already has one.
#
# Re-running it is safe. With the checkout already at the pinned commit it does
# nothing and exits zero.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

MM_COMMIT="6cb4e86540eca0d9ba62500a298385c9d863c8be"
MM_URL="https://github.com/littlefs-project/littlefs.git"

MM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_SOURCE="${MM_DIR}/upstream"

# The toplevel is checked before the commit, because an
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
    echo "littlefs already at ${MM_COMMIT}"
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

mm_from=${MM_LITTLEFS_SOURCE:-${MM_URL}}
echo "Cloning littlefs from ${mm_from} into ${MM_SOURCE}"
git clone --no-checkout "${mm_from}" "${MM_SOURCE}"
git -C "${MM_SOURCE}" checkout --quiet --detach "${MM_COMMIT}"

if ! mm_checked_out; then
    mm_head=$(git -C "${MM_SOURCE}" rev-parse HEAD 2>/dev/null || echo unknown)
    echo "vendor.sh: checkout is at ${mm_head}, expected ${MM_COMMIT}" >&2
    exit 65
fi
echo "littlefs checked out at ${MM_COMMIT}"
