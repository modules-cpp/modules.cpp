#!/bin/sh
# Kept for one release so that documentation, release notes, and habits keep
# working; the build is now build-analog.sh.
exec "$(dirname -- "$0")/build-analog.sh"  "$@"
