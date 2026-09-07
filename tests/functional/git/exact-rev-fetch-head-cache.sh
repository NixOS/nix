#!/usr/bin/env bash

source ../common.sh

requireGit

repo=$TEST_ROOT/repo

rm -rf "$repo" "$TEST_HOME/.cache/nix"

git init --initial-branch=master "$repo"
git -C "$repo" config user.email "nix-tests@example.com"
git -C "$repo" config user.name "Nix Tests"

echo valid > "$repo/file"
git -C "$repo" add file
git -C "$repo" commit -m 'Valid commit.'
valid_rev=$(git -C "$repo" rev-parse HEAD)

blob_oid=$(git -C "$repo" rev-parse HEAD:file)
git -C "$repo" tag -a bad-tag -m 'Annotated tag pointing to a blob.' "$blob_oid"
bad_tag_oid=$(git -C "$repo" rev-parse refs/tags/bad-tag)

export _NIX_FORCE_HTTP=1

# Fetching the annotated tag object succeeds at the Git layer, but Nix cannot
# use it as a commit. This leaves the tag object in FETCH_HEAD.
first_stderr=$TEST_ROOT/first-fetch.stderr
if nix eval --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; rev = \"$bad_tag_oid\"; }).outPath" \
    >/dev/null 2>"$first_stderr"; then
    echo "Expected fetchGit to reject an annotated tag pointing to a blob." >&2
    exit 1
fi

fetch_head=$(find "$TEST_HOME/.cache/nix/gitv3" -type f -name FETCH_HEAD -print -quit 2>/dev/null || true)
if [[ -z $fetch_head || ! -s $fetch_head ]] || ! grep -q "$bad_tag_oid" "$fetch_head"; then
    echo "Expected the failed tag fetch to populate FETCH_HEAD with the tag object." >&2
    cat "$first_stderr" >&2
    exit 1
fi

# A subsequent exact fetch must not be blocked by the invalid FETCH_HEAD entry.
second_stderr=$TEST_ROOT/second-fetch.stderr
if ! nix eval --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; rev = \"$valid_rev\"; }).outPath" \
    >/dev/null 2>"$second_stderr"; then
    echo "Expected a valid commit fetch to succeed after the invalid tag fetch." >&2
    cat "$second_stderr" >&2
    exit 1
fi
