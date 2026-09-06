#!/usr/bin/env bash

# Rev-pinned fetches used to leave the fetched commit un-refed (only in
# FETCH_HEAD), so a later rev-pinned fetch of the same URL had nothing to
# negotiate from and re-downloaded objects already on disk. Nix now fetches
# into refs/<reservedRefNamespace>/tip-<first two hex chars of the rev>, so a later fetch of a
# rev in the same bucket can negotiate against it.

source ../common.sh

requireGit

repo=$TEST_ROOT/repo
trace=$TEST_ROOT/git-trace

createGitRepo "$repo"

echo first > "$repo/first"
git -C "$repo" add first
git -C "$repo" commit -m 'First commit.'
rev1=$(git -C "$repo" rev-parse HEAD)

echo second > "$repo/second"
git -C "$repo" add second
git -C "$repo" commit -m 'Second commit.'
rev2=$(git -C "$repo" rev-parse HEAD)

export _NIX_FORCE_HTTP=1

nix-instantiate --eval --raw -E "(builtins.fetchGit { url = \"file://$repo\"; rev = \"$rev1\"; }).outPath" >/dev/null

reservedRefNamespace="__nix_internal_ref_namespace_reserved_for_fetchers_2c17c6393771ee3048ae34d6b380c5ec__"
cacheDir=$(find "$TEST_HOME/.cache/nix/gitv3" -maxdepth 1 -mindepth 1 -type d)

bucket1=${rev1:0:2}
if ! git -C "$cacheDir" rev-parse --verify -q "refs/$reservedRefNamespace/tip-$bucket1" >/dev/null; then
    echo "Expected refs/nix/tip-$bucket1 to exist after fetching $rev1." >&2
    exit 1
fi
if [[ $(git -C "$cacheDir" rev-parse "refs/$reservedRefNamespace/tip-$bucket1") != "$rev1" ]]; then
    echo "Expected refs/nix/tip-$bucket1 to point at $rev1." >&2
    exit 1
fi

export GIT_TRACE_PACKET=$trace
nix-instantiate --eval --raw -E "(builtins.fetchGit { url = \"file://$repo\"; rev = \"$rev2\"; }).outPath" >/dev/null
unset GIT_TRACE_PACKET

if ! grep -q "fetch> have $rev1" "$trace"; then
    echo "Expected the second fetch to advertise $rev1 (via refs/$reservedRefNamespace/tip-$bucket1) as a negotiation tip." >&2
    cat "$trace" >&2
    exit 1
fi

totals=$(grep -oE 'sideband< .*Total [0-9]+' "$trace" | sed -E 's/.*Total ([0-9]+)/\1/')
if [[ $totals -ge 6 ]]; then
    echo "Expected the second fetch to receive only new objects (got Total $totals, same as a full re-fetch would)." >&2
    exit 1
fi

bucket2=${rev2:0:2}
if [[ $(git -C "$cacheDir" rev-parse "refs/$reservedRefNamespace/tip-$bucket2") != "$rev2" ]]; then
    echo "Expected refs/nix/tip-$bucket2 to point at $rev2 after the second fetch." >&2
    exit 1
fi

if ! git -C "$cacheDir" cat-file -e "$rev1"; then
    echo "Expected $rev1 to remain resolvable after the second fetch." >&2
    exit 1
fi
