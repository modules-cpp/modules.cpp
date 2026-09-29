#!/bin/sh
# platforms/linux/apps/linux-smoke on this machine's architecture, twice.
#
# The Linux SDK binds seven interfaces and linux-smoke reaches every one, so it
# proves provider injection resolves them all at once. What it proves that no
# other build does is the tier: platform.linux.map is itself a platform
# interface, the SDK binds platform.linux.defaults to it, and the generic board
# overrides that one binding without disturbing the other six. So it builds
# with the SDK alone and then with the board, and scripts/build-linux.sh,
# reading the bindings, expects the map provider to swap and nothing else to
# move. target-smoke-any, which reaches no interface, must carry no provider.
#
# --run reports each facility; a desktop session refuses display and touch, so
# a non-zero exit there describes the machine and not the build. -a, -c,
# --keep, and --dry-run pass through to both builds.
set -eu

test_name=build-linux-smoke
. "$(dirname -- "$0")/lib/common.sh"
mm_enter_root

case " $* " in
    *" -h "*|*" --help "*)
        sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
esac

app=platforms/linux/apps/linux-smoke
echo "SDK alone: the map from the SDK"
sh scripts/build-linux.sh --board sdk --app "$app" --control target-smoke-any "$@"
echo
echo "Board over SDK: the map from the board"
sh scripts/build-linux.sh --board generic --app "$app" "$@"

case " $* " in *" --dry-run "*) exit 0 ;; esac

echo
echo "PASS: $test_name"
echo "Full qualification: run from a virtual terminal, with membership of the"
echo "video and input groups, where DRM master is available."
