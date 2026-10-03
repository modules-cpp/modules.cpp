#!/bin/sh
# Build one application for a native Linux lane, and verify what came out.
#
#   scripts/build-linux.sh --board generic|sdl|epaper|lcd|ili9341|sdk|BOARD --app APP
#                          [--arch aarch64|x86_64] [--compiler CXX]
#                          [--symbol PATTERN]... [--no-symbol PATTERN]...
#                          [--no-library NAME]... [--control APP]
#                          [--run] [--run-must-succeed] [--keep] [--dry-run]
#
# The lane is the target lane aimed at this machine's own triple, run by the
# native runner. --board names the board family for the architecture --
# generic, sdl, epaper, lcd, or ili9341 become <family>-linux-<arch> -- or sdk
# for the SDK alone, or any Linux board by name. APP is an application directory, or the
# name of one under apps/.
#
# What the image must contain is read from the manifests, exactly as
# scripts/build-pico.sh reads it: one initializer of every provider the
# application's closure reaches through the lane's bindings, none of every other
# provider the lane binds, the symbols of every driver a reached provider uses,
# and a NEEDED entry for every link input a library in the closure declares.
# --no-library names a library the image must not link, --symbol and
# --no-symbol demangled patterns, and --control a second application checked
# against its own closure.
#
# --run runs the image and explains its exit through the application's
# exit-codes file; --run-must-succeed makes a non-zero exit a failure, for a
# lane that needs nothing a desktop session withholds. --dry-run prints the
# lane, the commands, and every check, and touches nothing; it needs no
# toolchain, and with --arch no host compiler either. --keep leaves the lane
# configured afterwards; otherwise the configuration the tree had before is
# restored on every exit.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-linux
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
. "$(dirname -- "$0")/lib/manifest.sh"
mm_enter_root

board_argument=
app_argument=
control_argument=
symbols=
no_symbols=
no_libraries=
arch=
compiler=
run_app=no
run_must_succeed=no
keep=no
dry_run=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--board)
            mm_option_value "$#" "$1" "a board"
            board_argument=$2
            shift 2
            ;;
        --app)
            mm_option_value "$#" "$1" "an application"
            app_argument=$2
            shift 2
            ;;
        -a|--arch)
            mm_option_value "$#" "$1" "an architecture"
            arch=$2
            shift 2
            ;;
        -c|--compiler)
            mm_option_value "$#" "$1" "a compiler"
            compiler=$2
            shift 2
            ;;
        --symbol)
            mm_option_value "$#" "$1" "a pattern"
            symbols="$symbols $2"
            shift 2
            ;;
        --no-symbol)
            mm_option_value "$#" "$1" "a pattern"
            no_symbols="$no_symbols $2"
            shift 2
            ;;
        --no-library)
            mm_option_value "$#" "$1" "a library name"
            no_libraries="$no_libraries $2"
            shift 2
            ;;
        --control)
            mm_option_value "$#" "$1" "an application"
            control_argument=$2
            shift 2
            ;;
        --run)
            run_app=yes
            shift
            ;;
        --run-must-succeed)
            run_app=yes
            run_must_succeed=yes
            shift
            ;;
        --keep)
            keep=yes
            shift
            ;;
        --dry-run)
            dry_run=yes
            shift
            ;;
        -h|--help)
            sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

if [ -z "$board_argument" ] || [ -z "$app_argument" ]; then
    echo "$test_name: --board and --app are required; see --help" >&2
    exit 64
fi

mm_linux_resolve_arch
case "$board_argument" in
    generic|sdl|epaper|lcd|ili9341) mm_linux_lane "$board_argument" ;;
    sdk)
        mm_linux_lane generic
        board=
        ;;
    *)
        mm_linux_lane generic
        board=$board_argument
        ;;
esac
: "${compiler:=$target-g++}"
nm_command="$target-nm"
readelf_command="$target-readelf"

mm_resolve_app "$app_argument"
binary="out-target-$target/$app_path/$app"

bindings=$(mm_bindings "$sdk" "$board")
bound=$(mm_bound_providers "$sdk" "$board")
closure=$(mm_closure "$app_path/mm.mdy" "$bindings")
unbound=$(mm_unbound "$closure" "$bindings")
if [ -n "$unbound" ]; then
    echo "$test_name: $app reaches $(echo $unbound), which ${board:-$sdk} does not bind" >&2
    echo "  the build makes it unavailable on this lane" >&2
    exit 77
fi
expectations=$(mm_expectations "$closure" "$bound")

control_path=
if [ -n "$control_argument" ]; then
    saved_path=$app_path
    saved_app=$app
    mm_resolve_app "$control_argument"
    control_path=$app_path
    control_app=$app
    app_path=$saved_path
    app=$saved_app
    control_binary="out-target-$target/$control_path/$control_app"
    control_expectations=$(mm_expectations \
        "$(mm_closure "$control_path/mm.mdy" "$bindings")" "$bound")
fi

board_options=
[ -n "$board" ] && board_options=" --board $board"

if [ "$dry_run" = yes ]; then
    echo "$test_name: dry run"
    echo "  lane      $target $compiler $sdk ${board:-none}"
    echo "  configure ./configure --target $target --target-host --compiler $compiler --sdk $sdk$board_options --runner native --build debug"
    echo "  build     ./build --target $app_path/"
    echo "  image     $binary"
    printf '%s\n' "$expectations" | sed 's/^/  /'
    for name in $no_libraries; do echo "  no-library $name"; done
    for pattern in $symbols; do echo "  symbol $pattern"; done
    for pattern in $no_symbols; do echo "  no-symbol $pattern"; done
    if [ -n "$control_path" ]; then
        echo "  control   ./build --target $control_path/"
        printf '%s\n' "$control_expectations" | sed 's/^/  control /'
    fi
    if [ "$run_app" = yes ]; then
        echo "  run       ./$binary (must succeed: $run_must_succeed)"
    fi
    [ "$keep" = yes ] && echo "  keep      ${board:-$sdk}"
    exit 0
fi

mm_linux_prepare
case " $(printf '%s\n' "$expectations" | awk '$1 == "library" { printf "%s ", $2 }')" in
    *" SDL2 "*) mm_linux_require_sdl2 ;;
esac
mm_require_host_tools
if [ "$keep" = no ]; then
    mm_trap_restore_host
fi

echo "Native Linux lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    ${board:-none}"
echo "  app      $app"

if [ -n "$board" ]; then
    mm_linux_configure "$board"
else
    mm_linux_configure
fi
./build --target "$app_path/"

mm_linux_verify_image "$binary"
mm_verify_expectations "$binary" "$expectations"
for name in $(printf '%s\n' "$expectations" | awk '$1 == "library" { print $2 }'); do
    mm_linux_verify_library "$binary" "$name" yes
done
for name in $no_libraries; do mm_linux_verify_library "$binary" "$name" no; done
for pattern in $symbols; do mm_verify_symbol "$binary" "$pattern"; done
for pattern in $no_symbols; do mm_verify_no_symbol "$binary" "$pattern"; done

echo "  providers $(printf '%s\n' "$expectations" | awk '$1 == "provider" && $3 == 1 { printf "%s ", $2 }')"
echo "  drivers   $(printf '%s\n' "$expectations" | awk '$1 == "driver" { printf "%s ", $2 }')"

if [ -n "$control_path" ]; then
    ./build --target "$control_path/"
    mm_linux_verify_image "$control_binary"
    mm_verify_expectations "$control_binary" "$control_expectations"
    echo "  control   $control_app carries only its own closure's providers"
fi

explain_run() {
    mm_explain_exit "$app_path" "$1"
}

if [ "$run_app" = yes ]; then
    mm_linux_run
fi

if [ "$keep" = no ]; then
    mm_leave_host
fi

echo "PASS: $test_name ${board:-$sdk} $app"
