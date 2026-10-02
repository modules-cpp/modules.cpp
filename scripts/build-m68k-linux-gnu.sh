#!/bin/sh
# m68k Linux: the m68k-linux-glibc cross SDK, run by qemu-user with its
# runtime under /usr/m68k-linux-gnu. It binds no platform provider, so only
# applications that reach no platform interface build here.
#
# Every option passes through to scripts/build-target.sh, a later --app,
# --sdk, --board, --compiler, or --runner replacing the one given here; see
# its --help.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

exec sh "$(dirname -- "$0")/build-target.sh" --target m68k-linux-gnu --sdk m68k-linux-glibc --target-host --runner qemu-user --app target-smoke-any "$@"
