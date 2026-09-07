#!/bin/sh
# Debug one already-built application through the debug tool.

MM_BUILD="out"
${MM_BUILD}/bin/debug "$@"
