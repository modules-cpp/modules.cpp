#!/bin/sh
# Kept for one release so that documentation, release notes, and habits keep
# working; the build is now build-font.sh.
exec "$(dirname -- "$0")/build-font.sh" --board sdl "$@"
