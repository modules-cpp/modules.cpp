# The native Linux lane for the build scripts under scripts/. Sourced after
# lib/common.sh; defines functions and changes nothing when it is read.
#
# The lane is the target lane aimed at the build machine's own triple, run by
# the native runner, so only this machine's architecture can be built here.

# clang normalises a target triple to four fields and writes unknown where
# there is no vendor; Debian's GCC omits the field. aarch64-linux-gnu and
# aarch64-unknown-linux-gnu are the same machine, so collapse that one
# difference before comparing. none is left alone: a bare-metal triple means
# it.
mm_linux_normalize_triple() {
    case "$1" in
        *-unknown-*-*) printf '%s\n' "$1" | sed 's/-unknown-/-/' ;;
        *) printf '%s\n' "$1" ;;
    esac
}

# configure validates the native runner against the compiler the host lane is
# configured with, not against whatever gcc happens to be on PATH. Those
# differ on a machine whose host lane is clang, so ask the same compiler
# configure will.
mm_linux_configured_host_compiler() {
    if [ -f out/config.mdy ]; then
        sed -n 's/^host-compiler: *//p' out/config.mdy | head -1
    fi
}

# host_cxx, and arch from it when the caller named none.
mm_linux_resolve_arch() {
    host_cxx=$(mm_linux_configured_host_compiler)
    : "${host_cxx:=g++}"

    if [ -z "$arch" ]; then
        case $(mm_linux_normalize_triple "$(${host_cxx:-g++} -dumpmachine 2>/dev/null)") in
            aarch64-*) arch=aarch64 ;;
            x86_64-*)  arch=x86_64 ;;
            *)
                echo "$test_name: cannot infer architecture; pass --arch" >&2
                exit 64
                ;;
        esac
    fi
}

# target, sdk, machine, the generic board's map provider as linux_map, and the
# board of the given family -- generic, sdl, or epaper -- for arch.
mm_linux_lane() {
    case "$arch" in
        aarch64)
            target=aarch64-linux-gnu
            sdk=linux-aarch64
            linux_map=platform.linux.generic_aarch64.map
            machine=AArch64
            ;;
        x86_64)
            target=x86_64-linux-gnu
            sdk=linux-x86_64
            linux_map=platform.linux.generic_x86_64.map
            machine=X86-64
            ;;
        *)
            echo "$test_name: unsupported architecture: $arch" >&2
            exit 64
            ;;
    esac
    board="$1-linux-$arch"
}

# The compiler, nm, and readelf for the target, and the target being this
# machine's own triple, which is all the native runner accepts.
mm_linux_prepare() {
    : "${compiler:=$target-g++}"
    nm_command="$target-nm"
    readelf_command="$target-readelf"

    mm_command_hint="install the $target toolchain, or pass --arch"
    mm_require_commands "$compiler" "$nm_command" "$readelf_command"
    mm_command_hint=

    host_triple=$(mm_linux_normalize_triple "$("$host_cxx" -dumpmachine 2>/dev/null)")
    if [ "$host_triple" != "$(mm_linux_normalize_triple "$target")" ]; then
        echo "$test_name: $target is not this machine's triple" >&2
        echo "  $host_cxx reports $host_triple, and the native runner accepts only that" >&2
        exit 65
    fi
}

# The SDL library declares a system package, so an absent package is not an
# absent checkout. Nothing would catch it before the compiler did.
mm_linux_require_sdl2() {
    if [ ! -f /usr/include/SDL2/SDL.h ]; then
        echo "$test_name: SDL2 development headers not found" >&2
        echo "  install libsdl2-dev" >&2
        exit 65
    fi
}

# The lane, with a board when one is given. --target-host is not decoration:
# configure refuses the native runner without it, because a runner that
# executes the image directly only makes sense where the target is a hosted
# platform the build machine can run.
mm_linux_configure() {
    if [ "$#" -gt 0 ]; then
        ./configure \
            --target "$target" \
            --target-host \
            --compiler "$compiler" \
            --sdk "$sdk" \
            --board "$1" \
            --runner native \
            --build debug
    else
        ./configure \
            --target "$target" \
            --target-host \
            --compiler "$compiler" \
            --sdk "$sdk" \
            --runner native \
            --build debug
    fi
}

# A hosted executable is linked against the C library, so undefined symbols are
# expected and are not checked; that check belongs to a bare-metal image.
mm_linux_verify_image() {
    if [ ! -f "$1" ]; then
        echo "$test_name: missing artifact: $1" >&2
        exit 1
    fi
    mm_verify_elf "$1" "$machine" hosted
}

# A NEEDED entry for libSDL2, or none: the proof that a library's link-input
# reached the link line, or that an image which names no SDL provider did not
# pay for it. expected is yes or no; an optional third argument explains a
# missing entry further.
# A NEEDED entry for lib<name>, or none, in general.
mm_linux_verify_library() {
    image=$1
    name=$2
    expected=$3
    count=$("$readelf_command" -d "$image" | grep -c "Shared library: \\[lib$name" || true)
    if [ "$expected" = yes ] && [ "$count" -eq 0 ]; then
        echo "$test_name: $image does not link $name" >&2
        exit 1
    fi
    if [ "$expected" = no ] && [ "$count" -ne 0 ]; then
        echo "$test_name: $image links $name and should not" >&2
        exit 1
    fi
}

mm_linux_verify_sdl2() {
    image=$1
    expected=$2
    count=$("$readelf_command" -d "$image" | grep -c 'Shared library: \[libSDL2' || true)
    if [ "$expected" = yes ] && [ "$count" -eq 0 ]; then
        echo "$test_name: $image does not link SDL2" >&2
        if [ "$#" -gt 2 ]; then
            echo "  $3" >&2
        fi
        exit 1
    fi
    if [ "$expected" = no ] && [ "$count" -ne 0 ]; then
        echo "$test_name: $image links SDL2 and should not" >&2
        exit 1
    fi
}

# Run binary, report its exit through the caller's explain_run, and fail when
# run_must_succeed is yes and the run did not.
mm_linux_run() {
    echo
    echo "Run"
    set +e
    "./$binary"
    run_status=$?
    set -e
    echo "  $app exited $run_status"
    explain_run "$run_status"
    if [ "$run_must_succeed" = yes ] && [ "$run_status" -ne 0 ]; then
        echo "$test_name: $app was expected to succeed on this lane" >&2
        exit 1
    fi
}
