#!/bin/sh
# Native providers/sketch lifecycle; --pico adds SDK link and simulated flash.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/tests/scripts/common.sh"
case "${1:-}" in ''|--pico) ;; *) fail 'usage: external-platforms.sh [--pico]' ;; esac
pico_test=${1:-}
cp "$root/out/config.mdy" "$work/installation-config"
native_target=$(trap - EXIT; g++ -dumpmachine)
arch=${native_target%%-*}
case "$arch" in aarch64|x86_64) ;; *) fail "unsupported Linux host: $arch" ;; esac
mkdir "$work/native" "$work/sketch"
cat > "$work/native/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: app
name: native
use: mm.stdio
file: main.cpp
---
MANIFEST
cp "$root/apps/stdio-smoke/main.cpp" "$work/native/main.cpp"
cd "$work/native"
expect_status 0 "$root/configure.sh" --target "$native_target" --target-host --compiler clang++ --sdk "linux-$arch" --runner native
expect_status 0 "$root/build.sh" --target
expect_status 0 "$root/run.sh" --target
cd "$work/sketch"
cat > "$work/sketch/sketch.ino" <<'SKETCH'
void setup(){requestExit(0);}
void loop(){}
SKETCH
expect_status 0 "$root/sketch.sh" --external
if rg -q '^project:' mm.mdy; then fail 'portable sketch contains project locator'; fi
pass 'sketch has no installation locator'
expect_status 0 "$root/configure.sh" --host --compiler gcc
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
cat > "$work/sketch/sketch.ino" <<'SKETCH'
void setup(){requestExit(0);}
void loop(){ /* revised */ }
SKETCH
expect_failure "$root/run.sh" --host
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
rm main.cpp
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
if [ "$pico_test" = --pico ]; then
    [ -n "${picotool_DIR:-}" ] || fail 'export picotool_DIR for --pico'
    saved_picotool=$picotool_DIR
    mkdir "$work/pico" "$work/fake-tool"
    cp "$work/native/mm.mdy" "$work/pico/mm.mdy"
    cp "$root/apps/stdio-smoke/main.cpp" "$work/pico/main.cpp"
    cd "$work/pico"
    expect_status 0 "$root/configure.sh" --target arm-none-eabi --compiler arm-none-eabi-gcc --sdk pico-arm --board pico
    expect_status 0 "$root/build.sh" --target
    image="$work/pico/out-target-arm-none-eabi/native.uf2"
    [ -s "$image" ] || fail 'Pico UF2 missing or empty'
    pass 'Pico SDK produced UF2'
    cat > "$work/fake-tool/picotool" <<'BACKEND'
#!/bin/sh
printf '%s\n' "$@" > "$MM_TEST_FLASH_TRACE"
BACKEND
    chmod +x "$work/fake-tool/picotool"
    picotool_DIR="$work/fake-tool"
    MM_TEST_FLASH_TRACE="$work/flash-trace"
    export picotool_DIR MM_TEST_FLASH_TRACE
    expect_status 0 "$root/flash.sh"
    contains "$MM_TEST_FLASH_TRACE" 'load'
    contains "$MM_TEST_FLASH_TRACE" 'native.uf2'
    picotool_DIR=$saved_picotool
    export picotool_DIR
    expect_status 0 "$root/build.sh" --target
    cp out/config.mdy "$work/pico-config"
    expect_status 0 "$root/clean.sh"
    same out/config.mdy "$work/pico-config"
    absent out-target-arm-none-eabi
fi
same "$root/out/config.mdy" "$work/installation-config"
echo "PASS: external native/sketch/platform workflow ($checks checks)"
