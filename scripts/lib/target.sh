# Helpers for scripts/build-target.sh, which builds for any target lane
# configure accepts. Sourced after lib/common.sh and lib/manifest.sh; defines
# functions and changes nothing when it is read.

# The machine readelf reports for a triple's images, and the qemu user-mode
# emulator that runs a hosted one: machine and qemu_user.
mm_target_arch() {
    case "$1" in
        aarch64-*) machine=AArch64; qemu_user=qemu-aarch64 ;;
        arm-*)     machine=ARM;     qemu_user=qemu-arm ;;
        m68k-*)    machine=68000;   qemu_user=qemu-m68k ;;
        x86_64-*)  machine=X86-64;  qemu_user=qemu-x86_64 ;;
        *)
            echo "$test_name: no machine known for $1; add it to lib/target.sh" >&2
            exit 64
            ;;
    esac
}

# The C++ driver for a triple when the caller names none: <triple>-g++, or the
# newest <triple>-g++-N on PATH, since some distributions install only the
# versioned spelling. Prints <triple>-g++ when neither exists, so the failure
# names the plain spelling.
mm_target_find_compiler() {
    if command -v "$1-g++" >/dev/null 2>&1; then
        echo "$1-g++"
        return
    fi
    newest=
    for version in 20 19 18 17 16 15 14 13 12 11; do
        if command -v "$1-g++-$version" >/dev/null 2>&1; then
            newest="$1-g++-$version"
            break
        fi
    done
    echo "${newest:-$1-g++}"
}

# The SDK a board implies: the sdk of the first manifest along its
# derives-from chain that names one.
mm_target_board_sdk() {
    name=$1
    while [ -n "$name" ]; do
        manifest=$(mm_named_manifest board "$name") || {
            echo "$test_name: no board manifest for $name" >&2
            exit 64
        }
        value=$(mm_manifest_value "$manifest" sdk)
        if [ -n "$value" ]; then
            echo "$value"
            return
        fi
        name=$(mm_manifest_value "$manifest" derives-from)
    done
    echo "$test_name: board $1 names no SDK along its chain" >&2
    exit 64
}

# The SDK's target, runtime, and runtime prefix: sdk_target, runtime,
# runtime_prefix. A glibc runtime is hosted, anything else bare.
mm_target_sdk() {
    sdk_manifest=$(mm_named_manifest sdk "$1") || {
        echo "$test_name: no SDK manifest for $1" >&2
        exit 64
    }
    sdk_target=$(mm_manifest_value "$sdk_manifest" target)
    runtime=$(mm_manifest_value "$sdk_manifest" runtime)
    runtime_prefix=$(mm_manifest_value "$sdk_manifest" runtime-prefix)
    case "$runtime" in
        glibc) kind=hosted ;;
        *) kind=bare ;;
    esac
}

# The toolchain, and the runtime prefix an SDK installs into. configure
# refuses an SDK whose prefix is absent, but only after the saved record is
# in play; saying so first names the package.
mm_target_prepare() {
    nm_command="$target-nm"
    readelf_command="$target-readelf"
    mm_command_hint="install the $target cross toolchain, or pass --compiler"
    mm_require_commands "$compiler" "$nm_command" "$readelf_command"
    mm_command_hint=
    if [ -n "$runtime_prefix" ] && [ ! -d "$runtime_prefix" ]; then
        echo "$test_name: $sdk needs its runtime at $runtime_prefix, which is absent" >&2
        echo "  install the $target C library for cross-compiling, or pick another --sdk" >&2
        exit 65
    fi
}

# How a hosted image runs here: directly when the target is this machine's
# own triple, under qemu user mode with the SDK's runtime otherwise. Sets
# run_command, empty when neither is possible.
mm_target_hosted_runner() {
    host_triple=$(mm_linux_normalize_triple "$(${host_cxx:-g++} -dumpmachine 2>/dev/null)")
    run_command=
    if [ "$host_triple" = "$(mm_linux_normalize_triple "$target")" ]; then
        run_command=direct
    elif command -v "$qemu_user" >/dev/null 2>&1; then
        run_command=$qemu_user
    fi
}

# Run the image, explain a non-zero exit through the application's exit-codes file,
# and fail when run_must_succeed is yes and the run did not. A lane with a
# configured runner runs through ./run, as a person would; a hosted lane
# without one runs directly or under qemu user mode.
mm_target_run() {
    echo
    echo "Run"
    set +e
    if [ -n "$runner" ] && [ "$runner" != native ]; then
        ./run --target "$app_path/"
    elif [ "$run_command" = direct ]; then
        "./$binary"
    elif [ -n "$runtime_prefix" ]; then
        "$run_command" -L "$runtime_prefix" "./$binary"
    else
        "$run_command" "./$binary"
    fi
    run_status=$?
    set -e
    echo "  $app exited $run_status"
    [ "$run_status" -eq 0 ] || mm_explain_exit "$app_path" "$run_status"
    if [ "$run_must_succeed" = yes ] && [ "$run_status" -ne 0 ]; then
        echo "$test_name: $app was expected to succeed on this lane" >&2
        exit 1
    fi
}
