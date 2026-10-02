# The feature wrappers under scripts/. Sourced by a wrapper after it has set:
#
#   test_name         the wrapper's name, for its messages
#   wrapper_app       the application, as scripts/build-pico.sh --app takes it
#   wrapper_board     the board used when the caller names none
#   wrapper_both      options for either platform script
#   wrapper_pico      options for scripts/build-pico.sh only
#   wrapper_linux     options for scripts/build-linux.sh only
#   wrapper_usage     a line describing the wrapper for --help
#
# and, optionally, a wrapper_after function printing what to look for once the
# build passed. wrapper_main "$@" then reads --board, picks the platform script
# from the board's chain, and runs it with the wrapper's options followed by the
# caller's, so every platform option -- --flash, --arch, --run, --keep,
# --dry-run -- passes straight through.
#
# On the sdl and epaper Linux boards --run is --run-must-succeed: those lanes
# open a window and need nothing an ordinary desktop session withholds.

. "$(dirname -- "$0")/lib/manifest.sh"

wrapper_main() {
    board=$wrapper_board
    dry_run=no
    run_requested=no
    passed=
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -b|--board)
                mm_option_value "$#" "$1" "a board name"
                board=$2
                shift 2
                continue
                ;;
            -h|--help)
                echo "usage: $0 [--board BOARD] [platform options...]"
                echo "$wrapper_usage"
                echo "default board: $wrapper_board"
                echo "platform options: see scripts/build-pico.sh --help and"
                echo "  scripts/build-linux.sh --help"
                exit 0
                ;;
            --dry-run) dry_run=yes ;;
            --run) run_requested=yes ;;
        esac
        passed="$passed $1"
        shift
    done

    platform=$(mm_board_platform "$board") || {
        echo "$test_name: unknown board: $board" >&2
        exit 64
    }
    extra=$wrapper_both
    case "$platform" in
        pico) extra="$extra $wrapper_pico" ;;
        linux)
            extra="$extra $wrapper_linux"
            case "$board" in
                sdl|epaper|sdl-linux-*|epaper-linux-*)
                    if [ "$run_requested" = yes ]; then
                        # Word by word, not sed: \| alternation is GNU sed
                        # only, and BSD sed on macOS leaves --run unchanged.
                        rewritten=
                        for word in $passed; do
                            [ "$word" = --run ] && word=--run-must-succeed
                            rewritten="$rewritten $word"
                        done
                        passed=$rewritten
                    fi
                    ;;
            esac
            ;;
    esac

    # shellcheck disable=SC2086
    sh "$(dirname -- "$0")/build-$platform.sh" --board "$board" --app "$wrapper_app" \
        $extra $passed

    if [ "$dry_run" = no ] && command -v wrapper_after >/dev/null 2>&1; then
        wrapper_after
    fi
}
