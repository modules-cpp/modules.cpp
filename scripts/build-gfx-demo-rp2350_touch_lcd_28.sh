#!/bin/sh
# Kept for one release so that documentation, release notes, and habits keep
# working; the build is now build-gfx.sh.
exec "$(dirname -- "$0")/build-gfx.sh" --board rp2350_touch_lcd_28 "$@"
