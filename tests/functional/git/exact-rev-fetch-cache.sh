#!/usr/bin/env bash

source ../common.sh

requireGit

repo=$TEST_ROOT/repo
trace=$TEST_ROOT/git-trace

rm -rf "$repo" "$TEST_HOME/.cache/nix"

git init --initial-branch=master "$repo"
git -C "$repo" config user.email "nix-tests@example.com"
git -C "$repo" config user.name "Nix Tests"

echo first > "$repo/first"
git -C "$repo" add first
git -C "$repo" commit -m 'First commit.'
rev1=$(git -C "$repo" rev-parse HEAD)

echo second > "$repo/second"
git -C "$repo" add second
git -C "$repo" commit -m 'Second commit.'
rev2=$(git -C "$repo" rev-parse HEAD)

export GIT_TRACE_PACKET=$trace
export _NIX_FORCE_HTTP=1

nix eval --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"master\"; rev = \"$rev1\"; }).outPath" >/dev/null
second_stderr=$TEST_ROOT/second-fetch.stderr
nix eval --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"master\"; rev = \"$rev2\"; }).outPath" >/dev/null 2>"$second_stderr"

if grep -Eq -- "--negotiation-tip=refs/\\*|does not match any refs" "$second_stderr"; then
    echo "Git emitted an unmatched negotiation-tip warning during the second exact fetch." >&2
    cat "$second_stderr" >&2
    exit 1
fi

totals=$(grep -oE 'sideband< .*Total [0-9]+' "$trace" || true)
totals=$(printf '%s\n' "$totals" | sed -E 's/.*Total ([0-9]+)/\1/' | tr '\n' ' ')
totals=${totals% }

if ! grep -q "fetch> have $rev1" "$trace"; then
    echo "Expected fetch to advertise the first revision as a negotiation tip." >&2
    exit 1
fi

if [[ $totals != '3 3' ]]; then
    echo "Expected fetchGit to receive 3 objects per fetch, got: ${totals:-none}" >&2
    exit 1
fi
