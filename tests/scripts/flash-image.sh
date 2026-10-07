#!/bin/sh
# Run with out/bin/shell --run tests/scripts/flash-image.sh.
# The recording backend never contacts a device.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/tests/scripts/common.sh"
flash="$root/out/bin/flash"
[ -x "$flash" ] || fail 'build tools/flash first'
package="$work/tool path ' quoted"
mkdir "$package"
cat > "$package/picotool" <<'BACKEND'
#!/bin/sh
printf '%s\n' "$@" > "$MM_TEST_FLASH_TRACE"
exit "${MM_TEST_FLASH_EXIT:-0}"
BACKEND
chmod +x "$package/picotool"
image="$work/image; \$literal ' quoted.uf2"
printf 'backend fixture' > "$image"
MM_TEST_FLASH_TRACE="$work/trace"
picotool_DIR=$package
export MM_TEST_FLASH_TRACE picotool_DIR
cd "$work"
expect_status 0 "$flash" --image "$image"
printf 'load\n-v\n-x\n%s\n' "$image" > "$work/expected"
same "$work/expected" "$MM_TEST_FLASH_TRACE"
expect_status 0 "$flash" --auto-flash --image "$image"
printf 'load\n-f\n-v\n-x\n%s\n' "$image" > "$work/expected"
same "$work/expected" "$MM_TEST_FLASH_TRACE"
MM_TEST_FLASH_EXIT=23
export MM_TEST_FLASH_EXIT
expect_status 23 "$flash" --image "$image"
MM_TEST_FLASH_EXIT=0
export MM_TEST_FLASH_EXIT
reject() {
    rm -f "$MM_TEST_FLASH_TRACE"
    expect_status "$@"
    absent "$MM_TEST_FLASH_TRACE"
}
: > "$work/empty.uf2"
reject 64 "$flash" --image
reject 64 "$flash" --image ''
reject 64 "$flash" --image "$image" apps/camera-demo
reject 64 "$flash" --image "$image" --image "$image"
reject 64 "$flash" --image wrong.bin
reject 127 "$flash" --image "$work/missing.uf2"
reject 127 "$flash" --image "$work/empty.uf2"
reject 127 env -u picotool_DIR "$flash" --image "$image"
picotool_DIR="$work/absent"
export picotool_DIR
reject 127 "$flash" --image "$image"
reject 0 "$flash" --help
echo "PASS: standalone UF2 flashing ($checks checks)"
