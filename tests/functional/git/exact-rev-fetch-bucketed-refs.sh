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
pack=$TEST_ROOT/received.pack

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

reservedRefNamespace="__nix_internal_fetchers_48ae34d6b380c5ec__";

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
export GIT_TRACE_PACKFILE=$pack
nix-instantiate --eval --raw -E "(builtins.fetchGit { url = \"file://$repo\"; rev = \"$rev2\"; }).outPath" >/dev/null
unset GIT_TRACE_PACKET
unset GIT_TRACE_PACKFILE

if ! grep -q "fetch> have $rev1" "$trace"; then
    echo "Expected the second fetch to advertise $rev1 (via refs/$reservedRefNamespace/tip-$bucket1) as a negotiation tip." >&2
    cat "$trace" >&2
    exit 1
fi

# rev2 adds 3 new objects over rev1; a non-negotiated re-fetch would
# receive 6 (rev1's objects too). GIT_TRACE_PACKFILE creates an empty
# file when nothing was received, so guard on size before indexing it.
objectsReceived=0
if [[ -s "$pack" ]]; then
    git index-pack "$pack" >/dev/null
    objectsReceived=$(git verify-pack -v "$pack" | grep -c '^[0-9a-f]\{40\} ')
fi
if [[ $objectsReceived -ge 6 ]]; then
    echo "Expected the second fetch to receive only new objects (got $objectsReceived, same as a full re-fetch would)." >&2
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
