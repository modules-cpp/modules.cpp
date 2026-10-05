#!/bin/sh
# Build one application for a Pico board through the Pico SDK bridge, and
# verify what came out.
#
#   scripts/build-pico.sh --board BOARD --app APP [--symbol PATTERN]...
#                         [--no-symbol PATTERN]... [--abi SYMBOL]...
#                         [--connect-delay MS]
#                         [--control APP] [--flash] [--keep] [--dry-run]
#
# BOARD is any Pico board: one of the six the Pico SDK recognises, or a
# composite board under boards/, whose lane is its vendor ancestor's. APP is an
# application directory, or the name of one under apps/.
#
# What the image must contain is not written here. It is read from the
# manifests the way the build reads them: the application's closure, the board's
# bindings along its derives-from chain and its SDK's, and for every platform
# interface the closure reaches, the bound provider and its own uses, to a
# fixed point. The image must then carry one initializer of every provider in
# that closure and none of every other provider the lane binds -- the absences
# are as much the assertion as the presences -- and the symbols of every driver
# module a reached provider uses, which is what catches a board wired to an
# interface but not to its controller.
#
# --symbol and --no-symbol add demangled patterns the image must or must not
# contain, --abi raw symbols it must define, and --control a second
# application checked against its own closure, which for a portable
# application that reaches no interface is every provider absent.
#
# --connect-delay MS sets the post-enumeration USB CDC connect delay in
# milliseconds (default 500 ms), giving slow hosts time to bind cdc_acm and
# open the serial port before reporting connected.
#
# --dry-run prints the lane, the commands, and every check, and touches
# nothing; it needs no Pico tools. --keep leaves the board's lane configured
# afterwards, for flashing or debugging; otherwise the configuration the tree
# had before is restored on every exit. --flash writes the image with picotool
# while the lane is still configured; put the board in BOOTSEL first.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-pico
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/pico.sh"
. "$(dirname -- "$0")/lib/manifest.sh"
mm_enter_root

board=
app_argument=
control_argument=
symbols=
no_symbols=
abi=
flash_app=no
keep=no
dry_run=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--board)
            mm_option_value "$#" "$1" "a board name"
            board=$2
            shift 2
            ;;
        -a|--app)
            mm_option_value "$#" "$1" "an application"
            app_argument=$2
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
        --abi)
            mm_option_value "$#" "$1" "a symbol"
            abi="$abi $2"
            shift 2
            ;;
        --connect-delay)
            mm_option_value "$#" "$1" "a delay in milliseconds"
            case "$2" in
                ''|*[!0-9]*)
                    echo "$test_name: --connect-delay requires a non-negative integer: $2" >&2
                    exit 64
                    ;;
            esac
            MM_PICO_STDIO_USB_CONNECT_DELAY_MS=$2
            export MM_PICO_STDIO_USB_CONNECT_DELAY_MS
            shift 2
            ;;
        --control)
            mm_option_value "$#" "$1" "an application"
            control_argument=$2
            shift 2
            ;;
        --flash)
            flash_app=yes
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
            sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

if [ -z "$board" ] || [ -z "$app_argument" ]; then
    echo "$test_name: --board and --app are required; see --help" >&2
    exit 64
fi

mm_pico_lane "$board"
mm_resolve_app "$app_argument"
binary="out-target-$target/$app_path/$app"

bindings=$(mm_bindings "" "$board")
bound=$(mm_bound_providers "" "$board")
closure=$(mm_closure "$app_path/mm.mdy" "$bindings")
unbound=$(mm_unbound "$closure" "$bindings")
if [ -n "$unbound" ]; then
    echo "$test_name: $app reaches $(echo $unbound), which $board does not bind" >&2
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

if [ "$dry_run" = yes ]; then
    echo "$test_name: dry run"
    echo "  lane      $target $compiler $sdk $board"
    echo "  configure ./configure --target $target --compiler $compiler --sdk $sdk --board $board --build debug"
    echo "  build     ./build --target $app_path/"
    echo "  image     $binary"
    printf '%s\n' "$expectations" | sed 's/^/  /'
    for pattern in $symbols; do echo "  symbol $pattern"; done
    for pattern in $no_symbols; do echo "  no-symbol $pattern"; done
    for symbol in $abi; do echo "  abi $symbol"; done
    delay=${MM_PICO_STDIO_USB_CONNECT_DELAY_MS:-500}
    echo "  delay     ${delay} ms"
    echo "  uf2 $family"
    if [ -n "$control_path" ]; then
        echo "  control   ./build --target $control_path/"
        printf '%s\n' "$control_expectations" | sed 's/^/  control /'
    fi
    [ "$flash_app" = yes ] && echo "  flash     ./flash.sh $app_path/"
    [ "$keep" = yes ] && echo "  keep      $board"
    exit 0
fi

mm_pico_prepare
if [ "$keep" = no ]; then
    mm_trap_restore_host
fi

mm_pico_banner
echo "  board  $board"
echo "  app    $app"

mm_pico_configure
mm_pico_build "$app_path/"
mm_pico_verify_image "$binary"
mm_verify_expectations "$binary" "$expectations"
for pattern in $symbols; do mm_verify_symbol "$binary" "$pattern"; done
for pattern in $no_symbols; do mm_verify_no_symbol "$binary" "$pattern"; done
if [ -n "$abi" ]; then
    # shellcheck disable=SC2086
    mm_verify_defined "$binary" ABI $abi
fi
mm_pico_verify_uf2 "$binary"

echo "  providers $(printf '%s\n' "$expectations" | awk '$1 == "provider" && $3 == 1 { printf "%s ", $2 }')"
echo "  drivers   $(printf '%s\n' "$expectations" | awk '$1 == "driver" { printf "%s ", $2 }')"

if [ -n "$control_path" ]; then
    mm_pico_build "$control_path/"
    mm_verify_expectations "$control_binary" "$control_expectations"
    echo "  control   $control_app carries only its own closure's providers"
fi

if [ "$flash_app" = yes ]; then
    mm_pico_flash "$app_path/"
fi

if [ "$keep" = no ]; then
    mm_leave_host
fi

echo "PASS: $test_name $board $app"
