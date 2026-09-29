# Shared helpers for the build scripts under scripts/. Sourced, never run: it
# defines functions and changes nothing when it is read.
#
# Every function reports through the calling script's test_name, which the
# script sets before sourcing this file, and exits the script on failure with
# the status the scripts have always used: 64 for a usage error, 65 for a
# missing tool, 1 for a check that failed.
#
# The inspection helpers use nm_command and readelf_command, which the platform
# library sets from the lane's target triple.

# The repository root, from the path of the script that sourced this file, and
# the working directory every script runs from.
mm_enter_root() {
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
    cd "$script_dir"
}

# The value of an option that takes one, or a usage error naming what it takes.
# Called as: mm_option_value "$#" "$1" "an architecture"
mm_option_value() {
    if [ "$1" -lt 2 ]; then
        echo "$test_name: $2 requires $3" >&2
        exit 64
    fi
}

mm_unknown_argument() {
    echo "$test_name: unknown argument: $1" >&2
    exit 64
}

# Each command on PATH, or exit 65. mm_command_hint, when the caller sets it,
# is printed under the failure.
mm_require_commands() {
    for command_name in "$@"; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "$test_name: required command not found: $command_name" >&2
            if [ -n "${mm_command_hint:-}" ]; then
                echo "  $mm_command_hint" >&2
            fi
            exit 65
        fi
    done
}

mm_require_host_tools() {
    if [ ! -x out/bin/configure ] || [ ! -x out/bin/build ]; then
        echo "$test_name: host tools not found; run ./bootstrap.sh && ./build.sh first" >&2
        exit 65
    fi
}

# Restore the configuration the script found on any exit, keeping the exit
# status, or failing when the restore itself fails. The record is saved before
# the first configure and copied back, so a lane configured before the script
# ran -- a cross target, a board -- is what the tree has afterwards, not a
# host-only reset. A tree with no record is restored by ./configure, which is
# the host lane. mm_leave_host is the success path's counterpart: restore, and
# stop the trap from doing it again.
mm_save_host() {
    mm_saved_config=
    if [ -f out/config.mdy ]; then
        mm_saved_config=$(mktemp "${TMPDIR:-/tmp}/mm-config.XXXXXX")
        cp out/config.mdy "$mm_saved_config"
    fi
}

mm_put_back_host() {
    if [ -n "${mm_saved_config:-}" ] && [ -f "$mm_saved_config" ]; then
        cp "$mm_saved_config" out/config.mdy && rm -f "$mm_saved_config"
    else
        ./configure >/dev/null 2>&1
    fi
}

mm_restore_host() {
    status=$?
    trap - 0
    if ! mm_put_back_host; then
        echo "$test_name: failed to restore the host configuration" >&2
        [ "$status" -ne 0 ] || status=1
    fi
    exit "$status"
}

mm_trap_restore_host() {
    mm_save_host
    trap mm_restore_host 0
}

mm_leave_host() {
    mm_put_back_host
    trap - 0
}

# How many "initializer for module <provider>" lines the image carries: one per
# linked provider object.
mm_provider_count() {
    "$nm_command" -C "$1" | awk -v provider="$2" '
        index($0, "initializer for module " provider) { ++count }
        END { print count + 0 }
    '
}

# One initializer each. More than one would mean a provider object was linked
# twice; none would mean the interface resolved to nothing and the application
# is talking to an unserved fallback.
mm_verify_provider() {
    image=$1
    provider=$2
    expected=$3
    count=$(mm_provider_count "$image" "$provider")
    if [ "$count" -ne "$expected" ]; then
        echo "$test_name: expected $expected $provider initializer(s) in" >&2
        echo "  $image, got $count" >&2
        exit 1
    fi
}

# A demangled symbol matching the pattern must, or must not, be in the image.
mm_verify_symbol() {
    if ! "$nm_command" -C "$1" | grep -q "$2"; then
        echo "$test_name: no $2 symbols in $1" >&2
        exit 1
    fi
}

mm_verify_no_symbol() {
    if "$nm_command" -C "$1" | grep -q "$2"; then
        echo "$test_name: unexpected $2 symbols in $1" >&2
        exit 1
    fi
}

# Each named symbol defined in the image, by its raw name. what names the
# facility in the failure: "analog", "GPIO edge".
mm_verify_defined() {
    image=$1
    what=$2
    shift 2
    for symbol in "$@"; do
        if ! "$nm_command" "$image" | grep -Eq "[[:space:]]${symbol}$"; then
            echo "$test_name: missing $what symbol $symbol in $image" >&2
            exit 1
        fi
    done
}

# An ELF executable for the machine readelf names: EXEC for a bare-metal
# image; EXEC or DYN for a hosted one, since drivers default to a
# position-independent executable.
mm_verify_elf() {
    image=$1
    machine=$2
    kinds=$3
    if ! "$readelf_command" -h "$image" | awk -F: -v machine="$machine" -v kinds="$kinds" '
        $1 ~ /Type/ && (($2 ~ /EXEC/) || (kinds == "hosted" && $2 ~ /DYN/)) { executable = 1 }
        $1 ~ /Machine/ && index($2, machine) { expected_machine = 1 }
        END { exit !(executable && expected_machine) }
    '; then
        echo "$test_name: $image is not an executable for $machine" >&2
        exit 1
    fi
}

# The application directory an argument names: a directory with a manifest, or
# the directory of that name under apps/. Sets app_path and app.
mm_resolve_app() {
    if [ -f "$1/mm.mdy" ]; then
        app_path=${1%/}
    elif [ -f "apps/$1/mm.mdy" ]; then
        app_path=apps/${1%/}
    else
        echo "$test_name: no application at $1 or apps/$1" >&2
        exit 64
    fi
    app=$(basename "$app_path")
}

# Every "provider NAME COUNT" and "driver NAMESPACE" line of an expectation
# list, checked against an image.
mm_verify_expectations() {
    image=$1
    printf '%s\n' "$2" | while read -r kind name count; do
        case "$kind" in
            provider) mm_verify_provider "$image" "$name" "$count" ;;
            driver) mm_verify_symbol "$image" "$name" ;;
        esac
    done
}

# The lines of an application's exit-codes file that explain a status, or a
# pointer to its source when it has none or does not explain that status.
mm_explain_exit() {
    table="$1/exit-codes"
    if [ -f "$table" ] && grep -q "^$2 " "$table"; then
        sed -n "s/^$2 /  /p" "$table"
    else
        echo "  see $1/main.cpp for that step"
    fi
}
