#!/bin/sh
# Configure-managed mm: 1.3 module children shared by external apps.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/tests/scripts/common.sh"
for tool in configure build run shell; do
    [ -x "$root/out/bin/$tool" ] || fail "build tools/$tool first"
done
mkdir -p "$work/tree/base" "$work/tree/shared" "$work/tree/first" "$work/tree/second" "$work/tree/plain"
cat > "$work/tree/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: dir
name: tree
folder: base
folder: shared
folder: first
folder: second
folder: plain
---
MANIFEST
cat > "$work/tree/base/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: module
name: base
module: apps.base
file: detail.cppm apps.base:detail
file: base.cppm apps.base
---
MANIFEST
cat > "$work/tree/base/detail.cppm" <<'CPP'
export module apps.base:detail;
export int detail(){return 40;}
CPP
cat > "$work/tree/base/base.cppm" <<'CPP'
export module apps.base;
export import :detail;
export int base(){return detail();}
CPP
cat > "$work/tree/shared/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: module
name: shared
module: apps.shared
use: apps.base
use: mm.rtc
file: shared.cppm apps.shared
file: shared.cpp
---
MANIFEST
cat > "$work/tree/shared/shared.cppm" <<'CPP'
export module apps.shared;
export int answer();
CPP
cat > "$work/tree/shared/shared.cpp" <<'CPP'
module apps.shared;
import apps.base;
int answer(){return base()+2;}
CPP
for app in first second; do
    cat > "$work/tree/$app/mm.mdy" <<MANIFEST
---
mm: 1.3
kind: app
name: $app
use: apps.shared
file: main.cpp
---
MANIFEST
    cat > "$work/tree/$app/main.cpp" <<'CPP'
import apps.shared;
int main(){return answer()==42?0:1;}
CPP
done
cat > "$work/tree/plain/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: app
name: plain
file: main.cpp
---
MANIFEST
printf 'int main(){return 0;}\n' > "$work/tree/plain/main.cpp"
cd "$work/tree/first"
expect_status 0 "$root/configure.sh" --host --compiler gcc
contains "$work/tree/out/config.mdy" '^schema: external-configuration-1$'
absent "$work/tree/first/out/config.mdy"
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
cd "$work/tree/second"
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
cd "$work/tree/shared"
expect_status 0 "$root/build.sh" --host
expect_failure "$root/run.sh" --host
cd "$work/tree/plain"
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
nm -C "$work/tree/out-host/plain/plain" > "$work/plain-symbols"
if rg -q 'answer@apps.shared' "$work/plain-symbols"; then
    fail 'unused external module was linked into plain app'
fi
pass 'unused module omitted from plain executable'
# A module source change makes the prior app artifact stale.
cd "$work/tree/shared"
sed -i 's/base()+2/base()+3/' shared.cpp
cd "$work/tree/first"
expect_failure "$root/run.sh" --host
sed -i 's/answer()==42/answer()==43/' main.cpp
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
# Configure validates sources before publishing a replacement binding.
cp "$work/tree/out/config.mdy" "$work/stable-config"
cd "$work/tree/shared"
sed -i 's/export module apps.shared/export module apps.wrong/' shared.cppm
cd "$work/tree/first"
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'module declaration does not match'
same "$work/tree/out/config.mdy" "$work/stable-config"
cd "$work/tree/shared"
sed -i 's/export module apps.wrong/export module apps.shared/' shared.cppm
cd "$work/tree/first"
# App files cannot smuggle in a named module declaration.
sed -i 's/import apps.shared;/export module apps.sneaky;/' main.cpp
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'require kind: module'
sed -i 's/export module apps.sneaky;/import apps.shared;/' main.cpp
# Installed names cannot be shadowed, even when this module is otherwise valid.
sed -i 's/apps.shared/mm.rtc/g' "$work/tree/shared/mm.mdy" "$work/tree/shared/shared.cppm" "$work/tree/shared/shared.cpp"
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'exported by both'
sed -i 's/mm.rtc/apps.shared/g' "$work/tree/shared/shared.cppm" "$work/tree/shared/shared.cpp"
cat > "$work/tree/shared/mm.mdy" <<'MANIFEST'
---
mm: 1.3
kind: module
name: shared
module: apps.shared
use: apps.base
use: mm.rtc
file: shared.cppm apps.shared
file: shared.cpp
---
MANIFEST
# Dependency and interface errors are checked before configure publishes.
sed -i 's/use: apps.base/use: apps.unknown/' "$work/tree/shared/mm.mdy"
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'unknown module apps.unknown'
sed -i 's/use: apps.unknown/use: apps.base/' "$work/tree/shared/mm.mdy"
sed -i '/module: apps.base/a use: apps.shared' "$work/tree/base/mm.mdy"
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'dependency cycle'
sed -i '/use: apps.shared/d' "$work/tree/base/mm.mdy"
cd "$work/tree/shared"
cp shared.cppm duplicate.cppm
sed -i '/file: shared.cppm/a file: duplicate.cppm apps.shared' mm.mdy
cd "$work/tree/first"
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'duplicate external module interface'
cd "$work/tree/shared"
sed -i '/file: duplicate.cppm/d' mm.mdy
rm duplicate.cppm
sed -i 's/export module apps.shared/module apps.shared/' shared.cppm
cd "$work/tree/first"
expect_failure "$root/configure.sh" --host --compiler gcc
contains "$work/command.log" 'requires one exported primary interface'
cd "$work/tree/shared"
sed -i 's/^module apps.shared;/export module apps.shared;/' shared.cppm
cd "$work/tree/first"
sed -i '1i// export module apps.sneaky;' main.cpp
# Clang uses the same manifest, with its own BMI cache in the owned lane.
expect_status 0 "$root/configure.sh" --host --compiler clang++
expect_status 0 "$root/build.sh" --host
expect_status 0 "$root/run.sh" --host
cd "$work/tree"
expect_status 0 "$root/build.sh" --host
echo "PASS: external modules ($checks checks)"
