#!/bin/sh
# scripts/install-linux-deps.sh
#
# Installs Ubuntu/Debian system package dependencies for building, testing,
# and qualifying modules.cpp.
#
# Usage:
#   scripts/install-linux-deps.sh [options]
#
# Options:
#   -i, --interactive prompt interactively for components and confirmation
#   -y, --yes         run non-interactively (pass -y to apt-get)
#   -n, --dry-run     print packages and actions without executing them
#   --clang           install Clang compiler (clang)
#   --gcc             install GCC 15 compiler (g++-15, default)
#   --no-gcc          skip GCC 15 compiler installation
#   --target, --pico  install ARM bare-metal toolchain for Pico builds
#   --cross           install Linux cross-compilers and QEMU emulators
#   --all             install host dependencies, GCC, Clang, target, and cross
#   --no-compiler     skip all C++20 compilers (GCC and Clang)
#   -h, --help        display this help text and exit
#
# Packages installed:
#   Host platform libraries and tools:
#     build-essential   compiler basics, libc, linux kernel uapi headers (DRM)
#     libusb-1.0-0-dev  Linux USB host provider (platform.linux.usb.host)
#     libsdl2-dev       SDL2 display and touch providers, emulated displays
#     pkg-config        package configuration tool used by scripts and tests
#     cppcheck          static analysis required by ./check.sh and tests
#     cmake             required by Pico SDK external bridge
#     curl, unzip       required by Pico SDK host tools installer
#   Host C++20 compilers:
#     g++-15            GCC 15 with C++20 module support (unless --no-gcc)
#     clang             Clang compiler (with --clang or --all)
#   Target / Pico toolchain (with --target, --pico, or --all):
#     gcc-arm-none-eabi, binutils-arm-none-eabi, libnewlib-arm-none-eabi
#   Cross toolchain (with --cross or --all):
#     g++-aarch64-linux-gnu, g++-arm-linux-gnueabihf, qemu-user

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name="install-linux-deps"
interactive=false
assume_yes=false
dry_run=false
install_gcc=true
install_clang=false
install_target=false
install_cross=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        -i|--interactive)
            interactive=true
            ;;
        -y|--yes)
            assume_yes=true
            ;;
        -n|--dry-run)
            dry_run=true
            ;;
        --clang)
            install_clang=true
            ;;
        --gcc)
            install_gcc=true
            ;;
        --no-gcc)
            install_gcc=false
            ;;
        --target|--pico)
            install_target=true
            ;;
        --cross)
            install_cross=true
            ;;
        --all)
            install_gcc=true
            install_clang=true
            install_target=true
            install_cross=true
            ;;
        --no-compiler)
            install_gcc=false
            install_clang=false
            ;;
        -h|--help)
            sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "$test_name: unknown option: $1" >&2
            echo "Try '$0 --help' for more information." >&2
            exit 64
            ;;
    esac
    shift
done

if [ ! -f /etc/os-release ]; then
    echo "$test_name: /etc/os-release not found" >&2
    echo "  only Debian and Ubuntu are supported" >&2
    exit 65
fi

. /etc/os-release

is_debian_like=false
case "${ID:-}" in
    ubuntu|debian) is_debian_like=true ;;
esac
case "${ID_LIKE:-}" in
    *debian*|*ubuntu*) is_debian_like=true ;;
esac

if [ "$is_debian_like" != true ]; then
    echo "$test_name: unsupported distribution: ${ID:-unknown}" >&2
    echo "This script installs packages via apt-get for Debian and Ubuntu." >&2
    echo "Required packages:" >&2
    echo "  libusb-1.0 development library (libusb-1.0-0-dev)" >&2
    echo "  SDL2 development library (libsdl2-dev)" >&2
    echo "  pkg-config, cppcheck, cmake, curl, unzip, build-essential" >&2
    echo "  C++20 compiler with module support (g++-15 or clang++-16+)" >&2
    exit 65
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "$test_name: root privileges required" >&2
        echo "  run as root or install sudo" >&2
        exit 65
    fi
fi

if [ "$interactive" = true ]; then
    echo "=== modules.cpp Linux Dependency Setup ==="
    echo
    echo "Select C++20 compiler to install:"
    echo "  1) GCC 15 only"
    echo "  2) Clang only"
    echo "  3) Both GCC 15 and Clang"
    echo "  4) Neither (use existing compiler)"

    default_comp=1
    if [ "$install_gcc" = true ] && [ "$install_clang" = true ]; then
        default_comp=3
    elif [ "$install_clang" = true ]; then
        default_comp=2
    elif [ "$install_gcc" = false ]; then
        default_comp=4
    fi

    printf 'Choice [1-4] (default %s): ' "$default_comp"
    read -r comp_reply || comp_reply=""
    : "${comp_reply:=$default_comp}"
    case "$comp_reply" in
        1)
            install_gcc=true
            install_clang=false
            ;;
        2)
            install_gcc=false
            install_clang=true
            ;;
        3)
            install_gcc=true
            install_clang=true
            ;;
        4)
            install_gcc=false
            install_clang=false
            ;;
        *)
            echo "$test_name: invalid compiler selection: $comp_reply" >&2
            exit 64
            ;;
    esac

    echo
    if [ "$install_target" = true ]; then
        pico_prompt="Install ARM bare-metal toolchain for Pico? [Y/n]: "
    else
        pico_prompt="Install ARM bare-metal toolchain for Pico? [y/N]: "
    fi
    printf '%s' "$pico_prompt"
    read -r pico_reply || pico_reply=""
    if [ -n "$pico_reply" ]; then
        case "$pico_reply" in
            y|Y|yes|YES) install_target=true ;;
            *) install_target=false ;;
        esac
    fi

    echo
    if [ "$install_cross" = true ]; then
        cross_prompt="Install Linux cross-compilers and QEMU? [Y/n]: "
    else
        cross_prompt="Install Linux cross-compilers and QEMU? [y/N]: "
    fi
    printf '%s' "$cross_prompt"
    read -r cross_reply || cross_reply=""
    if [ -n "$cross_reply" ]; then
        case "$cross_reply" in
            y|Y|yes|YES) install_cross=true ;;
            *) install_cross=false ;;
        esac
    fi
fi

packages="build-essential libusb-1.0-0-dev libsdl2-dev pkg-config"
packages="$packages cppcheck cmake curl unzip"
needs_ppa=false
has_gcc15=false

if [ "$install_gcc" = true ]; then
    if apt-cache show g++-15 2>/dev/null | grep -qx 'Package: g++-15'; then
        has_gcc15=true
        packages="$packages g++-15"
    elif [ "${ID:-}" = ubuntu ]; then
        needs_ppa=true
        has_gcc15=true
        packages="$packages g++-15"
    else
        echo "$test_name: g++-15 not found in repository; installing g++" >&2
        packages="$packages g++"
    fi
fi

if [ "$install_clang" = true ]; then
    packages="$packages clang"
fi

if [ "$install_target" = true ]; then
    packages="$packages gcc-arm-none-eabi binutils-arm-none-eabi"
    packages="$packages libnewlib-arm-none-eabi"
fi

if [ "$install_cross" = true ]; then
    packages="$packages g++-aarch64-linux-gnu g++-arm-linux-gnueabihf qemu-user"
fi

if [ "$interactive" = true ]; then
    echo
    echo "Packages to install:"
    for pkg in $packages; do
        echo "  $pkg"
    done
    echo
    printf 'Proceed with installation? [Y/n]: '
    read -r confirm || confirm=""
    case "$confirm" in
        n|N|no|NO)
            echo "Aborted."
            exit 0
            ;;
        *)
            ;;
    esac
fi

apt_opts=""
if [ "$assume_yes" = true ]; then
    apt_opts="-y"
fi

if [ "$dry_run" = true ]; then
    echo "$test_name: dry run"
    if [ "$needs_ppa" = true ]; then
        echo "  $SUDO add-apt-repository -y ppa:ubuntu-toolchain-r/test"
    fi
    echo "  $SUDO apt-get update"
    if [ -n "$apt_opts" ]; then
        echo "  $SUDO apt-get install $apt_opts $packages"
    else
        echo "  $SUDO apt-get install $packages"
    fi
    if [ "$install_gcc" = true ] && [ "$has_gcc15" = true ]; then
        echo "  $SUDO update-alternatives \\"
        echo "    --install /usr/bin/g++ g++ /usr/bin/g++-15 100"
        echo "  $SUDO update-alternatives \\"
        echo "    --install /usr/bin/c++ c++ /usr/bin/g++-15 100"
    fi
    exit 0
fi

if [ "$needs_ppa" = true ]; then
    echo "$test_name: adding ppa:ubuntu-toolchain-r/test for g++-15..."
    if ! command -v add-apt-repository >/dev/null 2>&1; then
        echo "$test_name: installing software-properties-common..."
        $SUDO apt-get update
        if [ -n "$apt_opts" ]; then
            $SUDO apt-get install -y software-properties-common
        else
            $SUDO apt-get install software-properties-common
        fi
    fi
    $SUDO add-apt-repository -y ppa:ubuntu-toolchain-r/test
fi

echo "$test_name: updating package lists..."
$SUDO apt-get update

echo "$test_name: installing dependencies:"
for pkg in $packages; do
    echo "  $pkg"
done

# shellcheck disable=SC2086
if [ -n "$apt_opts" ]; then
    $SUDO apt-get install -y $packages
else
    $SUDO apt-get install $packages
fi

if [ "$install_gcc" = true ] && [ "$has_gcc15" = true ] && \
   [ -x /usr/bin/g++-15 ]; then
    echo "$test_name: configuring update-alternatives for g++ and c++..."
    $SUDO update-alternatives \
        --install /usr/bin/g++ g++ /usr/bin/g++-15 100
    $SUDO update-alternatives \
        --install /usr/bin/c++ c++ /usr/bin/g++-15 100
fi

echo
echo "PASS: $test_name"
echo "Dependencies installed successfully."
echo "Next steps:"
echo "  ./bootstrap.sh"
if [ "$install_clang" = true ] && [ "$install_gcc" = false ]; then
    echo "  ./configure --compiler clang++ --build debug"
else
    echo "  ./configure --build debug"
fi
echo "  ./build.sh"
echo "  ./test.sh"
