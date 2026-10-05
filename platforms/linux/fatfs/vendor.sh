#!/bin/sh
# Provisions the pinned FatFs the linux-fatfs library compiles for a program
# that links platform.linux.fs.fat, the provider of mm.fs.fat on the Linux
# SDKs. Linux's own copy: the Pico SDK bridge compiles another,
# platforms/pico/sdk/pico-sdk/fatfs, pinned on its own.
#
# FatFs is published as a zip, not a repository: this fetches ChaN's R0.16
# archive, refuses it unless its SHA-256 is the pinned one, and unpacks it into
# upstream/, ignored like the SDK's checkout. MM_FATFS_ARCHIVE names a local
# copy of the archive to use instead of the network. No tool runs it; a build
# that needs the checkout reports its absence, naming this script.
#
# Re-running it is safe. With upstream/ already holding the pinned archive it
# does nothing and exits zero.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

MM_VERSION="R0.16"
MM_URL="https://elm-chan.org/fsw/ff/arc/ff16.zip"
MM_SHA256="99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e"

MM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_SOURCE="${MM_DIR}/upstream"
MM_STAMP="${MM_SOURCE}/.mm-archive-sha256"

if [ -f "${MM_STAMP}" ] && [ "$(cat "${MM_STAMP}")" = "${MM_SHA256}" ] &&
   [ -f "${MM_SOURCE}/source/ff.c" ]; then
    echo "FatFs ${MM_VERSION} already unpacked"
    exit 0
fi
if [ -d "${MM_SOURCE}" ] && [ -n "$(ls -A "${MM_SOURCE}" 2>/dev/null)" ]; then
    echo "vendor.sh: ${MM_SOURCE} exists and is not the pinned archive; remove it first" >&2
    exit 65
fi

for command_name in unzip; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "vendor.sh: required command not found: ${command_name}" >&2
        exit 65
    fi
done
mm_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

mm_work=$(mktemp -d)
trap 'rm -rf "${mm_work}"' 0
mm_archive="${mm_work}/ff16.zip"
if [ -n "${MM_FATFS_ARCHIVE:-}" ]; then
    cp "${MM_FATFS_ARCHIVE}" "${mm_archive}"
elif command -v curl >/dev/null 2>&1; then
    echo "Fetching FatFs ${MM_VERSION} from ${MM_URL}"
    curl -fsSL -o "${mm_archive}" "${MM_URL}"
else
    echo "Fetching FatFs ${MM_VERSION} from ${MM_URL}"
    wget -q -O "${mm_archive}" "${MM_URL}"
fi

mm_actual=$(mm_sha256 "${mm_archive}")
if [ "${mm_actual}" != "${MM_SHA256}" ]; then
    echo "vendor.sh: the archive's SHA-256 is ${mm_actual}, expected ${MM_SHA256}" >&2
    exit 65
fi

mkdir -p "${MM_SOURCE}"
unzip -q "${mm_archive}" -d "${MM_SOURCE}"
echo "${MM_SHA256}" > "${MM_STAMP}"
echo "FatFs ${MM_VERSION} unpacked into ${MM_SOURCE}"
