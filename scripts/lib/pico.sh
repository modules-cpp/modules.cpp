# The Pico SDK lane for the build scripts under scripts/. Sourced after
# lib/common.sh; defines functions and changes nothing when it is read.

# The six boards the Pico SDK bridge recognises, and the lane each one means.
# A composite board -- one under boards/ -- means its vendor ancestor's lane,
# found by walking derives-from through the board manifests, so a new board
# needs no row here.
mm_pico_vendor_lane() {
    case "$1" in
        pico|pico-w)
            target=arm-none-eabi
            compiler=arm-none-eabi-gcc
            sdk=pico-arm
            family=rp2040
            ;;
        pico2-arm|pico2-w-arm)
            target=arm-none-eabi
            compiler=arm-none-eabi-gcc
            sdk=pico-arm
            family=rp2350
            ;;
        pico2-riscv|pico2-w-riscv)
            target=riscv32-pico-elf
            compiler=riscv32-pico-elf-gcc
            sdk=pico-riscv
            family=rp2350
            ;;
        *)
            return 1
            ;;
    esac
}

# The manifest that declares a board of this name, if one does.
mm_board_manifest() {
    for manifest in $(grep -rlx "name: $1" --include=mm.mdy boards platforms 2>/dev/null); do
        if grep -qx "kind: board" "$manifest"; then
            echo "$manifest"
            return 0
        fi
    done
    return 1
}

# target, compiler, sdk, family, nm_command, and readelf_command for a board,
# or a usage error naming it.
mm_pico_lane() {
    candidate=$1
    while ! mm_pico_vendor_lane "$candidate"; do
        manifest=$(mm_board_manifest "$candidate") || manifest=
        base=
        [ -n "$manifest" ] && base=$(sed -n 's/^derives-from: *//p' "$manifest" | head -1)
        if [ -z "$base" ]; then
            echo "$test_name: unsupported board: $1" >&2
            exit 64
        fi
        candidate=$base
    done
    nm_command="$target-nm"
    readelf_command="$target-readelf"
    case "$target" in
        arm-none-eabi) machine=ARM ;;
        riscv32-pico-elf) machine=RISC-V ;;
    esac
}

# The prebuilt Pico tools: MM_PICO_TOOLS, or platforms/pico/pico-sdk where
# platforms/pico/install-sdk-tools.sh puts them; picotool_DIR, or the bundle's
# picotool package; and the vendored SDK checkout. Sets mm_pico_tools,
# mm_picotool_dir, and mm_picotool, and puts the bundle's compilers on PATH.
mm_pico_find_tools() {
    mm_pico_tools=${MM_PICO_TOOLS:-"$script_dir/platforms/pico/pico-sdk"}
    if [ ! -d "$mm_pico_tools" ]; then
        echo "$test_name: Pico tools directory not found: $mm_pico_tools" >&2
        echo "  run platforms/pico/install-sdk-tools.sh to install them" >&2
        exit 65
    fi
    mm_pico_tools=$(CDPATH= cd -- "$mm_pico_tools" && pwd)

    mm_picotool_dir=${picotool_DIR:-"$mm_pico_tools/picotool"}
    if [ ! -d "$mm_picotool_dir" ]; then
        echo "$test_name: picotool package directory not found: $mm_picotool_dir" >&2
        exit 65
    fi
    mm_picotool_dir=$(CDPATH= cd -- "$mm_picotool_dir" && pwd)
    mm_picotool="$mm_picotool_dir/picotool"

    if [ ! -x "$mm_picotool" ]; then
        echo "$test_name: picotool executable not found: $mm_picotool" >&2
        exit 65
    fi
    if [ ! -f "$mm_picotool_dir/picotoolConfig.cmake" ] && \
       [ ! -f "$mm_picotool_dir/picotool-config.cmake" ]; then
        echo "$test_name: picotool CMake package not found in $mm_picotool_dir" >&2
        exit 65
    fi
    if [ ! -f platforms/pico/sdk/pico-sdk/upstream/README.md ]; then
        echo "$test_name: Pico SDK checkout is absent; run platforms/pico/sdk/pico-sdk/vendor.sh" >&2
        exit 65
    fi

    PATH="$mm_pico_tools/bin:$PATH"
    export PATH
}

# Tools, the lane's own commands, and the host tools, in that order.
mm_pico_prepare() {
    mm_pico_find_tools
    mm_require_commands "$compiler" "$nm_command" "$readelf_command" cmake
    mm_require_host_tools
}

mm_pico_banner() {
    echo "Pico SDK tools"
    echo "  bundle $mm_pico_tools"
    echo "  $($mm_picotool version)"
}

mm_pico_configure() {
    ./configure \
        --target "$target" \
        --compiler "$compiler" \
        --sdk "$sdk" \
        --board "$board" \
        --build debug
}

# One application or directory, with the picotool package the bridge
# requires.
mm_pico_build() {
    picotool_DIR="$mm_picotool_dir" ./build --target "$1"
}

# The five artifacts, no undefined symbols, and a bare-metal executable for
# the lane's machine.
mm_pico_verify_image() {
    binary=$1
    for artifact in \
        "$binary" \
        "$binary.bin" \
        "$binary.hex" \
        "$binary.elf.map" \
        "$binary.uf2"; do
        if [ ! -f "$artifact" ]; then
            echo "$test_name: missing artifact: $artifact" >&2
            exit 1
        fi
    done

    undefined=$("$nm_command" -u "$binary")
    if [ -n "$undefined" ]; then
        echo "$test_name: unexpected undefined symbols in $binary:" >&2
        echo "$undefined" >&2
        exit 1
    fi

    mm_verify_elf "$binary" "$machine" bare
}

# The UF2's block structure, and picotool's reading of it naming the lane's
# chip family.
mm_pico_verify_uf2() {
    binary=$1
    cmake \
        "-DMM_UF2=$binary.uf2" \
        -P platforms/pico/sdk/pico-sdk/cmake/validate-uf2.cmake

    if ! picotool_info=$($mm_picotool info "$binary.uf2" 2>&1); then
        echo "$test_name: picotool rejected $binary.uf2:" >&2
        echo "$picotool_info" >&2
        exit 1
    fi
    case "$picotool_info" in
        *"$family"*) ;;
        *)
            echo "$test_name: expected $family identity in $binary.uf2:" >&2
            echo "$picotool_info" >&2
            exit 1
            ;;
    esac
}

# Flashing happens while the lane is still configured for the board:
# ./flash.sh reads out/config.mdy to find the image, and restoring the host
# would point it back at the host lane.
mm_pico_flash() {
    echo
    echo "Flash"
    picotool_DIR="$mm_picotool_dir" ./flash.sh "$1"
}
