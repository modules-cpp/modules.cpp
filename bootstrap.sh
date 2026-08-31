#!/bin/sh
# Pawel Wodnicki (C) 2026
# 32bitmicro LLC (C) 2026
#
# shell script to bootstrap modules.cpp with host c++-20 compiler
#
# the shebang above is required: without it "set -e" has no effect when the
# script is executed directly
#
set -e

# Deliberately the literal c++, not $CXX: bootstrap must reach a working
# build1 the same way on every machine, independent of a caller's
# environment. $CXX is honoured only afterwards, by the self hosted build
# (mm::build::default_toolchain); see models/configuration/configuration.cppm.
MCCP="c++"
MCCP_VERSION=`$MCCP --version`
echo
echo "Compiler version"
echo
echo ${MCCP_VERSION}
echo 
MM_BUILD="out/"
echo "Build in ${MM_BUILD}"
echo
MM_CPPFLAGS="-std=c++20"
echo "Flags ${MM_CPPFLAGS}"
echo
mkdir -p ${MM_BUILD}
echo "Compile build0"
${MCCP} -v ${MM_CPPFLAGS} tools/build/main.cpp -o ${MM_BUILD}/build0 || exit $?
echo

# A build1 left over from an earlier run must not be able to satisfy the
# check below, or a stale executable would be accepted as this run's output
# and then used to build everything else.
rm -f "${MM_BUILD}/build1"

# Stage zero and the fallback below both compile module interfaces with raw
# compiler commands, and neither goes through mm::build::clear_module_cache.
# A gcm.cache left from an earlier build can hold BMIs for interfaces that
# have since changed, so it is cleared here rather than silently consumed.
rm -rf gcm.cache

# Hand the work to build0 rather than repeating the same fixed steps by hand
# a second time. The status is captured instead of ending the script,
# because the hand written commands below are exactly the fallback for
# build0's mode failing: exiting here would make them unreachable.
echo "Build build1"
mm_build1_status=0
"${MM_BUILD}/build0" build1 || mm_build1_status=$?
echo

if [ "${mm_build1_status}" -ne 0 ] || [ ! -x "${MM_BUILD}/build1" ]; then
    echo "bootstrap: build0 build1 failed (status ${mm_build1_status}) or produced no ${MM_BUILD}/build1" >&2
    echo "Build build1 with shell commands"

    mkdir -p "${MM_BUILD}/modules/mm/mdy/src"
    mkdir -p "${MM_BUILD}/modules/mm/build/src"
    mkdir -p "${MM_BUILD}/tools/build"

    MCCP_MODULES="${MCCP} -fmodules-ts"
    MM_MODULE_FLAGS="${MM_CPPFLAGS} -x c++"

    ${MCCP_MODULES} ${MM_MODULE_FLAGS} \
        -c modules/mm/mdy/mdy.cppm \
        -o "${MM_BUILD}/modules/mm/mdy/mdy.o" || exit $?

    ${MCCP_MODULES} ${MM_MODULE_FLAGS} \
        -c modules/mm/mdy/src/mdy.cpp \
        -o "${MM_BUILD}/modules/mm/mdy/src/mdy.o" || exit $?

    ${MCCP_MODULES} ${MM_MODULE_FLAGS} \
        -c modules/mm/build/build.cppm \
        -o "${MM_BUILD}/modules/mm/build/build.o" || exit $?

    ${MCCP_MODULES} ${MM_MODULE_FLAGS} \
        -c modules/mm/build/src/build.cpp \
        -o "${MM_BUILD}/modules/mm/build/src/build.o" || exit $?

    ${MCCP_MODULES} ${MM_MODULE_FLAGS} \
        -c tools/build/build.cpp \
        -o "${MM_BUILD}/tools/build/build.o" || exit $?

    # Linked to a temporary and renamed only on success, so a failed link
    # cannot leave a partial out/build1 behind for the check above, or for
    # the next run, to mistake for a working one.
    ${MCCP} ${MM_CPPFLAGS} \
        "${MM_BUILD}/modules/mm/mdy/mdy.o" \
        "${MM_BUILD}/modules/mm/mdy/src/mdy.o" \
        "${MM_BUILD}/modules/mm/build/build.o" \
        "${MM_BUILD}/modules/mm/build/src/build.o" \
        "${MM_BUILD}/tools/build/build.o" \
        -o "${MM_BUILD}/build1.tmp" || exit $?

    mv "${MM_BUILD}/build1.tmp" "${MM_BUILD}/build1" || exit $?
fi

# build1 walks the manifest tree from mm.mdy in the current directory,
# compiling, linking and installing every target it declares, tools/build's
# own "build" app included: this is the same thing build.sh does afterward,
# run once here so bootstrap.sh finishes with a fully built, installed
# project rather than stopping at build1.
echo "Build build"
"${MM_BUILD}/build1" || exit $?
