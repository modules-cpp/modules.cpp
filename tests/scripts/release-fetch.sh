#!/bin/sh
# scripts/release.sh must say why fetching origin's tags failed. A local tag
# that points elsewhere than origin's tag of the same name makes git refuse
# the fetch ("would clobber existing tag"), which the script once reported as
# "cannot reach origin" although origin was reachable.
#
#   tests/scripts/release-fetch.sh
#
# Builds a throwaway origin and clone, gives the clone a conflicting local
# tag, stubs gh so the authentication precondition passes, and runs the
# release script's dry run from the clone. Needs git and nothing else.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/mm-release-fetch.XXXXXX")
trap 'rm -rf "$work"' 0

fail() {
    echo "FAIL: release-fetch: $*" >&2
    exit 1
}

git init --quiet --bare "$work/origin.git"
git init --quiet "$work/seed"
(
    cd "$work/seed"
    git config user.email test@example.invalid
    git config user.name test
    mkdir scripts
    cp "$root/scripts/release.sh" scripts/release.sh
    git add scripts
    git commit --quiet -m first
    git tag v9.0.0
    echo second > second.txt
    git add second.txt
    git commit --quiet -m second
    git remote add origin "$work/origin.git"
    git push --quiet origin HEAD:refs/heads/main
    # origin's v9.0.0 is on the second commit ...
    git tag -f v9.0.0 HEAD >/dev/null
    git push --quiet origin refs/tags/v9.0.0
)
git clone --quiet --branch main "$work/origin.git" "$work/clone"
(
    cd "$work/clone"
    # ... and the clone's own v9.0.0 is on the first.
    git tag -f v9.0.0 HEAD~1 >/dev/null
)

mkdir "$work/bin"
printf '#!/bin/sh\nexit 0\n' > "$work/bin/gh"
chmod +x "$work/bin/gh"

status=0
output=$(cd "$work/clone" && PATH="$work/bin:$PATH" \
    sh scripts/release.sh --dry-run --no-verify v9.9.9 2>&1) || status=$?

[ "$status" -eq 65 ] || fail "expected exit 65, got $status: $output"
case "$output" in
    *"cannot reach origin"*) fail "reported an unreachable origin: $output" ;;
esac
case "$output" in
    *"v9.0.0"*) ;;
    *) fail "did not name the conflicting tag: $output" ;;
esac
case "$output" in
    *"differ"*) ;;
    *) fail "did not say the local and origin tags differ: $output" ;;
esac

echo "PASS: release-fetch"
