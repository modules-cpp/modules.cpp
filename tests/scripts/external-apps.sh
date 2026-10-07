#!/bin/sh
# Isolated installations prove binding, caching, defaults and guarded cleanup.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/tests/scripts/common.sh"
for tool in configure build run flash debug clean shell sketch; do
    [ -x "$root/out/bin/$tool" ] || fail "build tools/$tool first"
done
cp "$root/out/config.mdy" "$work/original-config"
installation() {
    dest=$1
    answer=$2
    mkdir -p "$dest/out/bin" "$dest/sample" "$dest/sdk"
    cat > "$dest/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: project
name: installation
folder: sample
folder: sdk
---
MANIFEST
    cat > "$dest/sample/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: module
name: sample
module: mm.sample
file: sample.cppm
---
MANIFEST
    printf 'export module mm.sample;\nexport int answer(){return %s;}\n' "$answer" > "$dest/sample/sample.cppm"
    triple=$(trap - EXIT; g++ -dumpmachine)
    printf '%s\n' '---' 'mm: 1.3' 'kind: sdk' 'name: native-sdk' "target: $triple" 'compiler-family: any' 'runtime: glibc' '---' > "$dest/sdk/mm.mdy"
    for tool in configure build run flash debug clean shell sketch; do
        cp "$root/out/bin/$tool" "$dest/out/bin/$tool"
        [ "$tool" = shell ] || cp "$root/$tool" "$root/$tool.sh" "$dest/"
    done
    printf 'installation sentinel\n' > "$dest/out/config.mdy"
}
app() {
    mkdir -p "$1"
    cat > "$1/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: app
name: sample-app
use: mm.sample
file: main.cpp
file: helper.cpp
---
MANIFEST
    cat > "$1/main.cpp" <<'CPP'
import mm.sample;
int helper();
int main(){return answer()==helper()?0:1;}
CPP
    printf 'int helper(){return 42;}\n' > "$1/helper.cpp"
}
a="$work/install-a"
b="$work/install-b"
installation "$a" 42
installation "$b" 84
cp "$a/out/config.mdy" "$work/config-a"
cp "$b/out/config.mdy" "$work/config-b"
app "$work/app"
cd "$work/app"
expect_failure "$a/build.sh" --host
absent out/config.mdy
expect_status 0 "$a/configure.sh" --host --compiler gcc
contains out/config.mdy '^mm: 3.0$'
contains out/config.mdy '^schema: external-configuration-1$'
contains out/config.mdy "$a"
expect_status 0 "$a/build.sh" --host
expect_status 0 "$a/run.sh" --host
# A second installation's consumers must honor the recorded A binding.
expect_status 0 "$b/build.sh" --host
expect_status 0 "$b/run.sh" --host
expect_status 0 "$b/build" --host .
expect_status 0 "$b/run" --host .
contains out/config.mdy "$a"
cp out/config.mdy "$work/stable-config"
expect_failure "$b/configure.sh" --host --compiler nonexistent-cxx
same out/config.mdy "$work/stable-config"
# Dependency failure cannot publish a new binding either.
mv "$b/sample" "$b/sample-hidden"
expect_failure "$b/configure.sh" --host --compiler gcc
same out/config.mdy "$work/stable-config"
mv "$b/sample-hidden" "$b/sample"
# Successful explicit reconfiguration changes the binding and invalidates outputs.
expect_status 0 "$b/configure.sh" --host --compiler gcc
expect_failure "$b/run.sh" --host
printf 'int helper(){return 84;}\n' > "$work/app/helper.cpp"
expect_status 0 "$b/build.sh" --host
expect_status 0 "$b/run.sh" --host
expect_status 0 "$a/configure.sh" --host --compiler gcc
printf 'int helper(){return 42;}\n' > "$work/app/helper.cpp"
expect_status 0 "$a/build.sh" --host
# Source changes block every artifact consumer before touching external services.
printf 'int helper(){return 43;}\n' > "$work/app/helper.cpp"
expect_failure "$a/run.sh" --host
expect_failure "$a/debug.sh" --host
expect_failure "$a/flash.sh"
printf 'int helper(){return 42;}\n' > "$work/app/helper.cpp"
expect_status 0 "$a/build.sh" --host
printf '\ncorrupted' >> "$work/app/out-host/sample-app"
expect_failure "$a/run.sh" --host
expect_status 0 "$a/build.sh" --host
expect_status 0 "$a/run.sh" --host
# A warning is fatal and failed compilation withdraws the last success record.
printf '[[nodiscard]] int value(){return 1;} int helper(){value(); return 42;}\n' > "$work/app/helper.cpp"
expect_failure "$a/build.sh" --host
contains "$work/command.log" 'error:'
expect_failure "$a/run.sh" --host
printf 'int helper(){return 42;}\n' > "$work/app/helper.cpp"
expect_status 0 "$a/build.sh" --host
# External named module declarations must fail before compilation.
printf 'export module mm.extra;\n' > "$work/app/helper.cpp"
expect_failure "$a/build.sh" --host
contains "$work/command.log" 'module'
printf 'module mm.sample;\n' > "$work/app/helper.cpp"
expect_failure "$a/build.sh" --host
printf 'int helper(){return 42;}\n' > "$work/app/helper.cpp"
expect_status 0 "$a/build.sh" --host
# Native initial target defaults and subsequent host/target preservation.
app "$work/native"
cd "$work/native"
triple=$(trap - EXIT; g++ -dumpmachine)
expect_status 0 "$a/configure.sh" --target "$triple" --target-host --compiler clang++ --sdk native-sdk --runner native
contains out/config.mdy '^host-compiler: clang\+\+$'
expect_status 0 "$a/build.sh" --target
expect_status 0 "$a/run.sh" --target
sed -n '/^cross-/p' out/config.mdy > "$work/target-lane"
expect_status 0 "$a/configure.sh" --host --compiler gcc
sed -n '/^cross-/p' out/config.mdy > "$work/target-after"
same "$work/target-lane" "$work/target-after"
sed -n '/^host-/p' out/config.mdy > "$work/host-lane"
expect_status 0 "$a/configure.sh" --target "$triple" --target-host --compiler clang++ --sdk native-sdk --runner native
sed -n '/^host-/p' out/config.mdy > "$work/host-after"
same "$work/host-lane" "$work/host-after"
# Connected directory projects share a single configuration/output root.
mkdir "$work/fleet"
cat > "$work/fleet/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: dir
name: fleet
folder: first
folder: second
---
MANIFEST
app "$work/fleet/first"
app "$work/fleet/second"
sed -i 's/name: sample-app/name: first/' "$work/fleet/first/mm.mdy"
sed -i 's/name: sample-app/name: second/' "$work/fleet/second/mm.mdy"
cd "$work/fleet/first"
expect_status 0 "$a/configure.sh" --host --compiler gcc
absent out/config.mdy
contains ../out/config.mdy "$work/fleet"
expect_status 0 "$a/build.sh" --host
expect_status 0 "$a/run.sh" --host
cd ..
expect_status 0 "$a/build.sh" --host
expect_failure "$a/run.sh" --host
expect_failure "$a/flash.sh"
expect_failure "$a/debug.sh" --host
[ -x out-host/first/first ] || fail 'missing first artifact'
[ -x out-host/second/second ] || fail 'missing second artifact'
pass 'child outputs are distinct'
cp out/config.mdy "$work/fleet-config"
cd second
expect_status 0 "$a/clean.sh"
same ../out/config.mdy "$work/fleet-config"
absent ../out-host
expect_status 0 "$a/build.sh" --host
expect_status 0 "$a/run.sh" --host
# Unregistered ancestors and missing cwd manifests must not be guessed.
app "$work/fleet/unregistered"
cd "$work/fleet/unregistered"
expect_status 0 "$a/configure.sh" --host --compiler gcc
contains out/config.mdy "$work/fleet/unregistered"
mkdir "$work/no-manifest"
cd "$work/no-manifest"
expect_failure "$a/configure.sh" --host --compiler gcc
absent out
# Explicit relative path and configure executable symlink keep cwd semantics.
expect_status 0 "$a/configure.sh" --host --compiler gcc ../app
expect_status 0 "$a/build.sh" --host ../app
expect_status 0 "$a/run.sh" --host ../app
ln -s "$a/out/bin/configure" "$work/configure-link"
expect_status 0 "$work/configure-link" --host --compiler gcc ../app
# Clang avoids GCC module mapper's whitespace restriction.
app "$work/app with spaces"
cd "$work/app with spaces"
expect_status 0 "$a/configure.sh" --host --compiler clang++
expect_status 0 "$a/build.sh" --host
expect_status 0 "$a/run.sh" --host
# Legacy locator combined with managed config is ambiguous, never mutated.
printf 'project: %s\n' "$a" > "$work/locator"
cp mm.mdy "$work/portable-manifest"
sed "/kind: app/a project: $a" "$work/portable-manifest" > "$work/app with spaces/mm.mdy"
expect_failure "$a/configure.sh" --host --compiler gcc
expect_failure "$a/build.sh" --host
cp "$work/portable-manifest" mm.mdy
# Standard clean is offline and preserves unowned directories and configuration.
cd "$work/app"
mkdir out-user
printf 'user sentinel\n' > "$work/app/out-user/sentinel"
cp out/config.mdy "$work/offline-config"
mv "$a" "$work/install-a-offline"
expect_status 0 "$b/clean.sh"
same out/config.mdy "$work/offline-config"
contains out-user/sentinel 'user sentinel'
absent out-host
mv "$work/install-a-offline" "$a"
expect_status 0 "$a/build.sh" --host
# Symlink artifacts reject cleanup before deletion and protect outside sentinels.
mkdir "$work/protected"
printf 'protected\n' > "$work/protected/sentinel"
ln -s "$work/protected" out-host/escape
expect_failure "$a/clean.sh"
contains "$work/protected/sentinel" 'protected'
[ -x out-host/sample-app ] || fail 'cleanup partially deleted artifacts'
pass 'cleanup refusal preserves artifacts'
rm out-host/escape
expect_failure "$a/clean.sh" --distclean --host
expect_status 0 "$a/clean.sh" --distclean
absent out
absent out-host
contains out-user/sentinel 'user sentinel'
expect_failure "$a/build.sh" --host
expect_status 0 "$a/configure.sh" --host --compiler gcc
expect_status 0 "$a/build.sh" --host
same "$a/out/config.mdy" "$work/config-a"
same "$b/out/config.mdy" "$work/config-b"
same "$root/out/config.mdy" "$work/original-config"
echo "PASS: external application lifecycle ($checks checks)"
