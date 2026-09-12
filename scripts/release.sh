#!/bin/sh
# Creates a release: a release-series branch, an annotated tag, and a GitHub
# release, all from the current HEAD.
#
#   scripts/release.sh [options] <version>
#
#   -n, --dry-run     print the plan and stop, changing nothing
#   -y, --yes         do not ask for confirmation
#       --no-verify   skip the build and test gate
#       --notes FILE  release notes; default is the commit subjects since the
#                     previous tag
#
# Version is vMAJOR.MINOR.PATCH. The series branch is derived from it, so
# v1.2.0 and v1.2.4 both live on release/v1.2.x, matching release/v1.0.x.
#
# Existing tags in this repository are lightweight. This script writes an
# annotated tag instead: a release should record who made it, when, and why,
# and `gh release create` uses the tag message when no notes are given.
#
# Nothing is pushed before the plan is shown and confirmed.
set -eu

dry_run=false
assume_yes=false
verify=true
notes_file=""
version=""

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--dry-run) dry_run=true ;;
        -y|--yes) assume_yes=true ;;
        --no-verify) verify=false ;;
        --notes)
            [ $# -ge 2 ] || { echo "release: --notes needs a file" >&2; exit 64; }
            notes_file=$2; shift
            ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        -*)
            echo "release: unknown option: $1" >&2; exit 64 ;;
        *)
            [ -z "$version" ] || { echo "release: one version only" >&2; exit 64; }
            version=$1
            ;;
    esac
    shift
done

fail() { echo "release: $*" >&2; exit 65; }

[ -n "$version" ] || { echo "usage: scripts/release.sh [-n] [-y] [--no-verify] [--notes FILE] <version>" >&2; exit 64; }

# vMAJOR.MINOR.PATCH, digits only.
case "$version" in
    v*.*.*) ;;
    *) fail "version must look like v1.2.0, got: $version" ;;
esac
printf '%s' "$version" | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+$' \
    || fail "version must look like v1.2.0, got: $version"

series="release/$(printf '%s' "$version" | sed 's/\.[0-9]*$/.x/')"

# ---- preconditions -------------------------------------------------------

git rev-parse --git-dir >/dev/null 2>&1 || fail "not a git repository"
cd "$(git rev-parse --show-toplevel)"

command -v gh >/dev/null 2>&1 || fail "gh is not installed"
gh auth status >/dev/null 2>&1 || fail "gh is not authenticated; run: gh auth login"

git remote get-url origin >/dev/null 2>&1 || fail "no origin remote"

[ -z "$(git status --porcelain)" ] || fail "working tree is not clean; commit or stash first"

# The specifications carry the release they describe. Releasing a version the
# documentation does not claim is almost always a mistake.
doc_release=$(grep -h '^release:' docs/*.mdy 2>/dev/null | sort -u | sed 's/^release: *//')
case "$doc_release" in
    "") echo "release: warning: no release marker found in docs/" >&2 ;;
    "$version") ;;
    *)
        printf 'release: docs/ declare release %s, not %s\n' "$doc_release" "$version" >&2
        printf '        update the release: markers in docs/*.mdy first\n' >&2
        exit 65
        ;;
esac

# Check the remote, not only the local refs: a stale remote-tracking branch can
# name something origin does not actually have, and the reverse is worse.
git fetch --quiet --tags origin || fail "cannot reach origin"

git rev-parse -q --verify "refs/tags/$version" >/dev/null \
    && fail "tag $version already exists locally"
[ -z "$(git ls-remote --tags origin "refs/tags/$version")" ] \
    || fail "tag $version already exists on origin"

head_commit=$(git rev-parse HEAD)
head_short=$(git rev-parse --short HEAD)

# The series branch is created at HEAD when absent. When present, HEAD must be
# a descendant of it, so the release only ever moves the branch forward.
branch_action="create $series at $head_short"
if git ls-remote --heads origin "refs/heads/$series" | grep -q .; then
    remote_tip=$(git ls-remote --heads origin "refs/heads/$series" | cut -f1)
    if [ "$remote_tip" = "$head_commit" ]; then
        branch_action="$series already at $head_short"
    elif git merge-base --is-ancestor "$remote_tip" "$head_commit" 2>/dev/null; then
        branch_action="fast-forward $series to $head_short"
    else
        fail "$series on origin is not an ancestor of HEAD; rebase or release from that branch"
    fi
fi

# ---- verification --------------------------------------------------------

if [ "$verify" = true ]; then
    echo "Verifying the tree before releasing"
    [ -x out/bin/build ] || fail "out/bin/build missing; run ./bootstrap.sh first"
    ./build.sh >/dev/null 2>&1 || fail "build failed; not releasing"
    ./test.sh  >/dev/null 2>&1 || fail "tests failed; not releasing"
    echo "  build and tests pass"
fi

# ---- notes ---------------------------------------------------------------

previous_tag=$(git describe --tags --abbrev=0 2>/dev/null || true)
notes_source="commit subjects since ${previous_tag:-the first commit}"
if [ -n "$notes_file" ]; then
    [ -f "$notes_file" ] || fail "notes file not found: $notes_file"
    notes_source="$notes_file"
fi

# ---- plan ----------------------------------------------------------------

cat <<PLAN

Release plan
  version        $version
  commit         $head_short  $(git log -1 --pretty=%s)
  series branch  $branch_action
  tag            annotated $version at $head_short
  github release $version from $series
  notes          $notes_source
  remote         $(git remote get-url origin)

PLAN

if [ "$dry_run" = true ]; then
    echo "Dry run: nothing was changed."
    exit 0
fi

if [ "$assume_yes" != true ]; then
    printf 'Create and push this release? [y/N] '
    read -r reply
    case "$reply" in
        y|Y|yes|YES) ;;
        *) echo "Aborted."; exit 0 ;;
    esac
fi

# ---- publish -------------------------------------------------------------

echo "Pushing $series"
git push --quiet origin "HEAD:refs/heads/$series"

echo "Tagging $version"
git tag --annotate "$version" --message "modules.cpp $version"
git push --quiet origin "refs/tags/$version"

echo "Creating the GitHub release"
if [ -n "$notes_file" ]; then
    gh release create "$version" --target "$series" --title "$version" --notes-file "$notes_file"
elif [ -n "$previous_tag" ]; then
    git log --pretty='- %s' "$previous_tag..HEAD" \
        | gh release create "$version" --target "$series" --title "$version" --notes-file -
else
    git log --pretty='- %s' \
        | gh release create "$version" --target "$series" --title "$version" --notes-file -
fi

echo
echo "Released $version on $series"
