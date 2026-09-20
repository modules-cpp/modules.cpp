#!/bin/sh
# Provisions the pinned prebuilt Pico tools: the RISC-V toolchain, picotool,
# pioasm, and OpenOCD.
#
# The companion of platforms/pico/sdk/pico-sdk/vendor.sh. That script provisions
# the SDK source a build compiles; this one provisions the host tools a build and
# its qualification invoke. Neither is ever run by a tool: provisioning is a
# deliberate act, and a build only ever reports that a prerequisite is absent.
#
# The installation is large and machine-local, so platforms/pico/pico-sdk/ is
# ignored. This script is the tracked record of what belongs there.
#
# Re-running it is safe. With the pinned tools already installed it does nothing
# and exits zero, so it can be named in setup notes without qualification.
set -eu

MM_TAG="v2.3.1-0"
MM_SDK_TOOLS="2.3.1"
MM_PICOTOOL="2.3.1"
MM_RISCV="16"
MM_OPENOCD="0.12.0+dev"
MM_URL="https://github.com/raspberrypi/pico-sdk-tools/releases/download/${MM_TAG}"

MM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_TARGET=${MM_PICO_TOOLS:-"${MM_DIR}/pico-sdk"}

# Reused across runs when set, so a reprovision after rm -rf does not re-fetch
# the toolchain. Unset, every download lands in the work directory and goes away
# with it.
MM_CACHE=${MM_PICO_TOOLS_CACHE:-}

# Published by the release. Recorded per host architecture because the archives
# differ, and checked before extraction because these become the tools that
# compile and sign firmware.
mm_digest() {
    case "$1" in
        mac:sdk-tools)    echo 7d4c8966f8f47b08c7b2aaf513648f321a15753fab6e8f30bd79d1e6c921aa2f ;;
        mac:picotool)     echo 7bb3cea5d9f1b000fc758cf4b54774fda5c16a9899d342ef6b0c30138988793b ;;
        mac:openocd)      echo 412caeab08a4613d3fd425fc1fa3fa84aa95ff05bca008f13eacee118e05035d ;;
        # The macOS RISC-V asset is universal and its release digest is not
        # published in the release metadata. It is still recorded in the
        # installation stamp after extraction and target validation.
        mac:riscv)        echo - ;;
        aarch64:sdk-tools) echo 108a3e1d15ea8ace1a7efc8751b36d0a2b5e1d8e1cf230ee2f061e3c8f1c8276 ;;
        aarch64:picotool)  echo 8cce5579c92e77c0439f34d4f49d37b1dc7d1e7c455233398f2e7b49599f67aa ;;
        aarch64:riscv)     echo b7a2c0365ac45fad9dbf384b236738c00045170b59fcfa7350185bac33df0289 ;;
        aarch64:openocd)   echo 7b2d56d74ca27485bfc5a72fd59ac6684877a41e09abddcc18475cc9291adff5 ;;
        x86_64:sdk-tools)  echo 39758f147a8e615f8c77586ce3c85a25c6728910a86612eea184fc86eb606992 ;;
        x86_64:picotool)   echo 878a1a1e7d8336b8be60300c7218f366d9790c0d3a211b181d6d172e80b1248e ;;
        x86_64:riscv)      echo fc36f1b37f99de54a358115443638023de34e15fe199e718e50cca4d212e7e9f ;;
        x86_64:openocd)    echo 7b2ad4bb310356c7e50c17d0a813f7f803765ea4a0610a2191909767835f3b0d ;;
        *) return 1 ;;
    esac
}

mm_arch=$(uname -m)
case "${mm_arch}" in
    aarch64|arm64) mm_arch=aarch64 ;;
    x86_64|amd64) mm_arch=x86_64 ;;
    *)
        echo "install-sdk-tools.sh: no pinned Pico tools for ${mm_arch}" >&2
        echo "install-sdk-tools.sh: the release publishes aarch64-lin and x86_64-lin only" >&2
        exit 65
        ;;
esac

mm_os=$(uname -s)
case "${mm_os}" in
    Linux) mm_suffix=lin; mm_archive_ext=tar.gz ;;
    Darwin) mm_suffix=mac; mm_archive_ext=zip ;;
    *)
        echo "install-sdk-tools.sh: no pinned Pico tools for ${mm_os}" >&2
        exit 65
        ;;
esac
mm_digest_arch=${mm_suffix}
if [ "${mm_suffix}" = lin ]; then
    mm_digest_arch=${mm_arch}
fi

# Names the exact contents, so an installation left by a different release or a
# different architecture is recognised as one rather than silently accepted.
MM_STAMP=".mm-pico-tools"
mm_stamp_text() {
    echo "tag ${MM_TAG}"
    echo "arch ${mm_arch}"
    echo "sdk-tools ${MM_SDK_TOOLS} $(mm_digest "${mm_digest_arch}:sdk-tools")"
    echo "picotool ${MM_PICOTOOL} $(mm_digest "${mm_digest_arch}:picotool")"
    echo "riscv ${MM_RISCV} $(mm_digest "${mm_digest_arch}:riscv")"
    echo "openocd ${MM_OPENOCD} $(mm_digest "${mm_digest_arch}:openocd")"
}

if [ -f "${MM_TARGET}/${MM_STAMP}" ]; then
    if [ "$(cat "${MM_TARGET}/${MM_STAMP}")" = "$(mm_stamp_text)" ]; then
        echo "Pico tools ${MM_TAG} (${mm_arch}) already installed in ${MM_TARGET}"
        exit 0
    fi
    echo "install-sdk-tools.sh: ${MM_TARGET} holds a different installation:" >&2
    sed 's/^/    /' "${MM_TARGET}/${MM_STAMP}" >&2
    echo "install-sdk-tools.sh: remove it and re-run to reprovision" >&2
    exit 65
fi

if [ -d "${MM_TARGET}" ] && [ -n "$(ls -A "${MM_TARGET}" 2>/dev/null)" ]; then
    echo "install-sdk-tools.sh: ${MM_TARGET} exists and is not a pinned installation; remove it first" >&2
    exit 65
fi

for mm_tool in curl unzip; do
    command -v "${mm_tool}" >/dev/null 2>&1 || {
        echo "install-sdk-tools.sh: ${mm_tool} not found" >&2
        exit 65
    }
done
if command -v sha256sum >/dev/null 2>&1; then
    mm_sha256_kind=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    mm_sha256_kind=shasum
else
    echo "install-sdk-tools.sh: sha256sum or shasum not found" >&2
    exit 65
fi

# Beside the target rather than in the system temporary directory, so the
# finished tree is moved into place on one filesystem and the move is atomic.
MM_WORK="${MM_TARGET}.incoming.$$"
mm_cleanup() {
    rm -rf "${MM_WORK}"
}
trap mm_cleanup EXIT INT TERM

rmdir "${MM_TARGET}" 2>/dev/null || true
mkdir -p "$(dirname -- "${MM_TARGET}")"
rm -rf "${MM_WORK}"
mkdir "${MM_WORK}"

if [ -n "${MM_CACHE}" ]; then
    mkdir -p "${MM_CACHE}"
    MM_CACHE=$(CDPATH= cd -- "${MM_CACHE}" && pwd)
else
    MM_CACHE="${MM_WORK}"
fi

# Verified whether it was just downloaded or came from the cache: a cache entry
# is no more trusted than a fresh transfer.
mm_fetch() {
    mm_key="$1"
    mm_file="$2"
    mm_want=$(mm_digest "${mm_digest_arch}:${mm_key}")
    mm_path="${MM_CACHE}/${mm_file}"

    if [ ! -f "${mm_path}" ]; then
        echo "Fetching ${mm_file}"
        curl --fail --location --proto '=https' --tlsv1.2 --show-error --silent \
            --output "${mm_path}.part" "${MM_URL}/$(mm_encode "${mm_file}")"
        mv "${mm_path}.part" "${mm_path}"
    fi

    if [ "${mm_sha256_kind}" = sha256sum ]; then
        mm_got=$(sha256sum "${mm_path}" | cut -d' ' -f1)
    else
        mm_got=$(shasum -a 256 "${mm_path}" | cut -d' ' -f1)
    fi
    if [ "${mm_want}" != "-" ] && [ "${mm_got}" != "${mm_want}" ]; then
        echo "install-sdk-tools.sh: ${mm_file} is not the pinned archive" >&2
        echo "install-sdk-tools.sh:   expected ${mm_want}" >&2
        echo "install-sdk-tools.sh:   measured ${mm_got}" >&2
        rm -f "${mm_path}"
        exit 65
    fi

    case "${mm_archive_ext}" in
        tar.gz) tar -xzf "${mm_path}" -C "${MM_WORK}" ;;
        zip) unzip -q "${mm_path}" -d "${MM_WORK}" ;;
    esac
}

# The OpenOCD archive carries a + in its name, which is not a literal in a URL
# path. Nothing else in the set needs encoding.
mm_encode() {
    echo "$1" | sed 's/+/%2B/g'
}

echo "Installing Pico tools ${MM_TAG} (${mm_arch}) into ${MM_TARGET}"
if [ "${mm_suffix}" = mac ]; then
    mm_fetch riscv "riscv-toolchain-${MM_RISCV}-mac.zip"
    mm_fetch picotool "picotool-${MM_PICOTOOL}-mac.zip"
    mm_fetch sdk-tools "pico-sdk-tools-${MM_SDK_TOOLS}-mac.zip"
    mm_fetch openocd "openocd-${MM_OPENOCD}-mac.zip"
else
    mm_fetch riscv "riscv-toolchain-${MM_RISCV}-${mm_arch}-lin.tar.gz"
    mm_fetch picotool "picotool-${MM_PICOTOOL}-${mm_arch}-lin.tar.gz"
    mm_fetch sdk-tools "pico-sdk-tools-${MM_SDK_TOOLS}-${mm_arch}-lin.tar.gz"
    mm_fetch openocd "openocd-${MM_OPENOCD}-${mm_arch}-lin.tar.gz"
fi

# Checked before the tree is moved into place, so a partial or rearranged
# release never becomes the installation the test scripts find.
mm_require_exec() {
    [ -x "${MM_WORK}/$1" ] || {
        echo "install-sdk-tools.sh: ${1} is missing or not executable in the extracted tools" >&2
        exit 65
    }
}
mm_require_file() {
    [ -f "${MM_WORK}/$1" ] || {
        echo "install-sdk-tools.sh: ${1} is missing from the extracted tools" >&2
        exit 65
    }
}

mm_require_exec bin/riscv32-pico-elf-gcc
mm_require_exec bin/riscv32-pico-elf-g++
mm_require_exec picotool/picotool
mm_require_file picotool/picotoolConfig.cmake
mm_require_file picotool/picotoolConfigVersion.cmake
mm_require_exec pioasm/pioasm
mm_require_file pioasm/pioasmConfig.cmake
mm_require_exec openocd
[ -d "${MM_WORK}/scripts" ] || {
    echo "install-sdk-tools.sh: the OpenOCD scripts directory is missing from the extracted tools" >&2
    exit 65
}

mm_target=$("${MM_WORK}/bin/riscv32-pico-elf-gcc" -dumpmachine)
if [ "${mm_target}" != "riscv32-pico-elf" ]; then
    echo "install-sdk-tools.sh: the toolchain reports target ${mm_target}, expected riscv32-pico-elf" >&2
    exit 65
fi

mm_stamp_text > "${MM_WORK}/${MM_STAMP}"
mv "${MM_WORK}" "${MM_TARGET}"
trap - EXIT INT TERM

echo "Pico tools ${MM_TAG} (${mm_arch}) installed in ${MM_TARGET}"
echo "  $("${MM_TARGET}/bin/riscv32-pico-elf-gcc" --version | awk 'NR == 1 { print; exit }')"
echo "  $("${MM_TARGET}/picotool/picotool" version | awk 'NR == 1 { print; exit }')"
