#!/bin/sh
# Provisions the pinned Pico SDK checkout for the pico-sdk library.
#
# Found by convention beside the library manifest, the way the CMake bridge is
# found at cmake/. No tool invokes this script: provisioning is a deliberate act,
# and a build only ever reports that the checkout is absent.
#
# Re-running it is safe. With the checkout already at the pinned commit it does
# nothing and exits zero, so it can be named in setup notes without qualification.
set -eu

MM_TAG="2.3.1"
MM_COMMIT="079c6f39023649b154152db30f1d781e884879bc"
MM_URL="https://github.com/raspberrypi/pico-sdk.git"

MM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_SOURCE="${MM_DIR}/upstream"

# git rev-parse inside an empty or non-repository directory answers for whichever
# repository encloses it, so the toplevel is checked before the commit. Without
# that, an empty upstream/ reports this project's own HEAD and compares unequal
# for the wrong reason.
mm_checked_out() {
    [ -e "${MM_SOURCE}/.git" ] || return 1
    mm_top=$(git -C "${MM_SOURCE}" rev-parse --show-toplevel 2>/dev/null) || return 1
    [ "${mm_top}" = "${MM_SOURCE}" ] || return 1
    mm_head=$(git -C "${MM_SOURCE}" rev-parse HEAD 2>/dev/null) || return 1
    [ "${mm_head}" = "${MM_COMMIT}" ] || return 1
    [ -z "$(git -C "${MM_SOURCE}" status --porcelain=v1 --untracked-files=all --ignore-submodules=none)" ] || return 1
    git -C "${MM_SOURCE}" submodule status --recursive |
        while IFS= read -r mm_submodule; do
            case "${mm_submodule}" in
                " "*) ;;
                *) exit 1 ;;
            esac
        done
}

if mm_checked_out; then
    echo "pico-sdk ${MM_TAG} already at ${MM_COMMIT}"
    exit 0
fi

if [ -e "${MM_SOURCE}/.git" ]; then
    mm_head=$(git -C "${MM_SOURCE}" rev-parse HEAD 2>/dev/null || echo unknown)
    if [ "${mm_head}" = "${MM_COMMIT}" ]; then
        echo "vendor.sh: ${MM_SOURCE} is dirty or has incomplete or drifted submodules at ${MM_COMMIT}" >&2
    else
        echo "vendor.sh: ${MM_SOURCE} holds ${mm_head}, not the pinned ${MM_COMMIT}" >&2
    fi
    echo "vendor.sh: remove it and re-run to reprovision" >&2
    exit 65
fi

if [ -d "${MM_SOURCE}" ] && [ -n "$(ls -A "${MM_SOURCE}" 2>/dev/null)" ]; then
    echo "vendor.sh: ${MM_SOURCE} exists and is not a checkout; remove it first" >&2
    exit 65
fi

rmdir "${MM_SOURCE}" 2>/dev/null || true

echo "Cloning pico-sdk ${MM_TAG} into ${MM_SOURCE}"
git clone --branch "${MM_TAG}" --recurse-submodules "${MM_URL}" "${MM_SOURCE}"

if ! mm_checked_out; then
    mm_head=$(git -C "${MM_SOURCE}" rev-parse HEAD 2>/dev/null || echo unknown)
    echo "vendor.sh: tag ${MM_TAG} resolved to ${mm_head}, expected ${MM_COMMIT}" >&2
    exit 65
fi

echo "pico-sdk ${MM_TAG} checked out at ${MM_COMMIT}"
