#!/bin/sh
# Kept for one release so that documentation, release notes, and habits keep
# working; the build is now build-epaper.sh.
exec "$(dirname -- "$0")/build-epaper.sh" --app gfx --board epaper "$@"
