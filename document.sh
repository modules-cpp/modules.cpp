#!/bin/sh
#
# generates HTML documentation for the manifest tree
#
# Fits after the normal sequence:
#
#     ./clean.sh        remove everything generated
#     ./bootstrap.sh    build stage 0, which builds the stage 1 build tool
#     ./build.sh        build the project with it
#     ./test.sh         run every test target
#     ./document.sh     render the project as HTML
#
# All the work is done by the mdy app, so this script only locates it and passes
# arguments through. It walks the manifest tree and writes one page per
# manifest, mirroring the source layout: the project manifest becomes
# out/index.html and docs/mm.mdy becomes out/docs/index.html. Each page
# carries breadcrumbs, links to its children, its front matter and its prose.
#
#     ./document.sh                    the whole tree, into out
#     ./document.sh -o=site            somewhere else, e.g. to publish
#     ./document.sh modules/mm.mdy     a subtree, rooted at the output
#     ./document.sh -v                 verbose, passed through to the app
#
# A kind:doc or kind:module manifest names one document rather than a tree, and
# the app writes that to stdout instead:
#
#     ./out/bin/mdy docs/mdy.mdy -h > mdy.html
#
# the shebang above is required: without it "set -eu" has no effect when the
# script is executed directly
#
set -eu

cd "$(dirname "$0")"

MM_MANIFEST="mm.mdy"
MM_VERBOSE="false"
MM_HAVE_OUTPUT="false"
MM_OUTPUT=""

for arg in "$@"; do
    case "${arg}" in
        -v|--verbose) MM_VERBOSE="true" ;;
        -o=*)
            MM_HAVE_OUTPUT="true"
            MM_OUTPUT=${arg#-o=}
            ;;
        -h|--help)
            echo "usage: document.sh [-v] [-o=<dir>] [<manifest>]"
            exit 0
            ;;
        -*)
            echo "document: unexpected option: ${arg}" >&2
            echo "usage: document.sh [-v] [-o=<dir>] [<manifest>]" >&2
            exit 64
            ;;
        *) MM_MANIFEST="${arg}" ;;
    esac
done

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
MM_TOOL="out/bin/mdy"

if [ ! -x "${MM_TOOL}" ]; then
    echo "document: ${MM_TOOL} not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

# -h is the app's flag for HTML, not for help.
if [ "${MM_VERBOSE}" = "true" ] && [ "${MM_HAVE_OUTPUT}" = "true" ]; then
    exec "${MM_TOOL}" -v "-o=${MM_OUTPUT}" -h "${MM_MANIFEST}"
elif [ "${MM_VERBOSE}" = "true" ]; then
    exec "${MM_TOOL}" -v -h "${MM_MANIFEST}"
elif [ "${MM_HAVE_OUTPUT}" = "true" ]; then
    exec "${MM_TOOL}" "-o=${MM_OUTPUT}" -h "${MM_MANIFEST}"
else
    exec "${MM_TOOL}" -h "${MM_MANIFEST}"
fi
