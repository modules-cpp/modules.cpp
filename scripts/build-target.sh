#!/bin/sh
# Build one application for any target lane configure accepts, and verify it.
#
#   scripts/build-target.sh --target TRIPLE (--sdk SDK | --board BOARD)
#                           --app APP [--compiler CXX] [--target-host]
#                           [--runner PROFILE] [--symbol PATTERN]...
#                           [--no-symbol PATTERN]... [--control APP]
#                           [--run] [--run-must-succeed] [--keep] [--dry-run]
#
# The per-platform wrappers -- build-aarch64-linux-gnu.sh,
# build-arm-linux-gnueabihf.sh, build-arm-none-eabi.sh, build-m68k-linux-gnu.sh,
# build-m68k-linux-external.sh, build-x86_64-linux-gnu.sh -- name a lane and a
# default application over this script, and every option given to one of them
# passes through, a later --app, --sdk, --board, or --compiler replacing the
# wrapper's.
#
# --board implies its SDK. The compiler defaults to TRIPLE-g++, or the newest
# versioned TRIPLE-g++-N installed. --target-host lets the lane build
# applications restricted to the host capability; --runner is configure's.
# With no --runner, a hosted lane aimed at this machine's own triple gets the
# native runner.
#
# The checks are read from the manifests, exactly as scripts/build-pico.sh
# reads them: one initializer of every provider the application's closure
# reaches through the lane's bindings, none of every other provider the lane
# binds, the symbols of every driver a reached provider uses, and a NEEDED
# entry for every link input a library in the closure declares. An
# application reaching an interface the lane does not bind is unavailable,
# exit 77. The image must be an ELF executable for the triple's machine.
#
# --run runs the image: through ./run when the lane has a runner of its own,
# otherwise directly on this machine's triple or under qemu user mode with
# the SDK's runtime prefix. The exit is explained through the application's
# exit-codes file; --run-must-succeed makes a non-zero exit a failure.
# --dry-run prints the lane, the commands, and every check, and touches
# nothing. --keep leaves the lane configured; otherwise the configuration the
# tree had before is restored on every exit.
set -eu

test_name=build-target
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
. "$(dirname -- "$0")/lib/manifest.sh"
. "$(dirname -- "$0")/lib/target.sh"
mm_enter_root

target=
sdk=
board=
compiler=
target_host=no
runner=
app_argument=
control_argument=
symbols=
no_symbols=
run_app=no
run_must_succeed=no
keep=no
dry_run=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        -t|--target)
            mm_option_value "$#" "$1" "a target triple"
            target=$2
            shift 2
            ;;
        --sdk)
            mm_option_value "$#" "$1" "an SDK"
            sdk=$2
            board=
            shift 2
            ;;
        -b|--board)
            mm_option_value "$#" "$1" "a board"
            board=$2
            sdk=
            shift 2
            ;;
        -c|--compiler)
            mm_option_value "$#" "$1" "a compiler"
            compiler=$2
            shift 2
            ;;
        --target-host)
            target_host=yes
            shift
            ;;
        --runner)
            mm_option_value "$#" "$1" "a runner profile"
            runner=$2
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
            sed -n '2,37p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

[ "$runner" = none ] && runner=
if [ -z "$target" ] || [ -z "$app_argument" ] || { [ -z "$sdk" ] && [ -z "$board" ]; }; then
    echo "$test_name: --target, --sdk or --board, and --app are required; see --help" >&2
    exit 64
fi

mm_target_arch "$target"
[ -n "$board" ] && sdk=$(mm_target_board_sdk "$board")
mm_target_sdk "$sdk"
if [ "$sdk_target" != "$target" ]; then
    echo "$test_name: $sdk is an SDK for $sdk_target, not $target" >&2
    exit 64
fi
[ -n "$compiler" ] || compiler=$(mm_target_find_compiler "$target")

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

lane_options=
if [ -n "$board" ]; then
    lane_options=" --board $board"
else
    lane_options=" --sdk $sdk"
fi
[ "$target_host" = yes ] && lane_options=" --target-host$lane_options"

if [ "$dry_run" = yes ]; then
    runner_options=
    [ -n "$runner" ] && runner_options=" --runner $runner"
    echo "$test_name: dry run"
    echo "  lane      $target $compiler $sdk ${board:-none} $kind"
    echo "  configure ./configure --target $target --compiler $compiler$lane_options$runner_options --build debug"
    if [ -z "$runner" ] && [ "$kind" = hosted ]; then
        echo "  runner    native when this machine is $target"
    fi
    echo "  build     ./build --target $app_path/"
    echo "  image     $binary"
    echo "  elf       $machine $kind"
    printf '%s\n' "$expectations" | sed '/^$/d; s/^/  /'
    for pattern in $symbols; do echo "  symbol $pattern"; done
    for pattern in $no_symbols; do echo "  no-symbol $pattern"; done
    if [ -n "$control_path" ]; then
        echo "  control   ./build --target $control_path/"
        printf '%s\n' "$control_expectations" | sed '/^$/d; s/^/  control /'
    fi
    if [ "$run_app" = yes ]; then
        if [ -n "$runner" ] && [ "$runner" != native ]; then
            how="./run --target $app_path/"
        elif [ "$kind" = hosted ]; then
            how="./$binary, or $qemu_user${runtime_prefix:+ -L $runtime_prefix} on another machine"
        else
            how="nothing: a bare lane runs only through --runner"
        fi
        echo "  run       $how (must succeed: $run_must_succeed)"
    fi
    [ "$keep" = yes ] && echo "  keep      ${board:-$sdk}"
    exit 0
fi

host_cxx=$(mm_linux_configured_host_compiler)
: "${host_cxx:=g++}"
mm_target_prepare
case " $(printf '%s\n' "$expectations" | awk '$1 == "library" { printf "%s ", $2 }')" in
    *" SDL2 "*) mm_linux_require_sdl2 ;;
esac
if [ "$run_app" = yes ]; then
    if [ -n "$runner" ] && [ "$runner" != native ]; then
        :
    elif [ "$kind" = hosted ]; then
        mm_target_hosted_runner
        if [ -z "$run_command" ]; then
            echo "$test_name: --run needs $qemu_user to run $target here" >&2
            exit 65
        fi
    else
        echo "$test_name: --run on a bare lane needs --runner" >&2
        exit 64
    fi
fi
if [ -z "$runner" ] && [ "$kind" = hosted ] && [ "$target_host" = yes ]; then
    mm_target_hosted_runner
    [ "$run_command" = direct ] && runner=native
fi
mm_require_host_tools
if [ "$keep" = no ]; then
    mm_trap_restore_host
fi

echo "Target lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    ${board:-none}"
echo "  runner   ${runner:-none}"
echo "  app      $app"

# shellcheck disable=SC2086
./configure --target "$target" --compiler "$compiler" $lane_options \
    ${runner:+--runner "$runner"} --build debug
./build --target "$app_path/"

if [ ! -f "$binary" ]; then
    echo "$test_name: missing artifact: $binary" >&2
    exit 1
fi
mm_verify_elf "$binary" "$machine" "$kind"
mm_verify_expectations "$binary" "$expectations"
for name in $(printf '%s\n' "$expectations" | awk '$1 == "library" { print $2 }'); do
    mm_linux_verify_library "$binary" "$name" yes
done
for pattern in $symbols; do mm_verify_symbol "$binary" "$pattern"; done
for pattern in $no_symbols; do mm_verify_no_symbol "$binary" "$pattern"; done

echo "  providers $(printf '%s\n' "$expectations" | awk '$1 == "provider" && $3 == 1 { printf "%s ", $2 }')"
echo "  drivers   $(printf '%s\n' "$expectations" | awk '$1 == "driver" { printf "%s ", $2 }')"

if [ -n "$control_path" ]; then
    ./build --target "$control_path/"
    mm_verify_elf "$control_binary" "$machine" "$kind"
    mm_verify_expectations "$control_binary" "$control_expectations"
    echo "  control   $control_app carries only its own closure's providers"
fi

if [ "$run_app" = yes ]; then
    mm_target_run
fi

if [ "$keep" = no ]; then
    mm_leave_host
fi

echo "PASS: $test_name $target ${board:-$sdk} $app"
