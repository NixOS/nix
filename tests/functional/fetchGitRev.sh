#!/usr/bin/env bash

source common.sh

requireGit

# Exercise the remote-fetch cache path even though the fixture uses file://.
export _NIX_FORCE_HTTP=1
export GIT_CONFIG_GLOBAL="$TEST_ROOT/gitconfig"
# New caches start with HEAD on master; the remote below uses main at a newer revision.
git config --global init.defaultBranch master

repo=$TEST_ROOT/repo
createGitRepo "$repo"
echo pinned > "$repo/content"
git -C "$repo" add content
git -C "$repo" commit -m pinned
rev=$(git -C "$repo" rev-parse HEAD)
git -C "$repo" checkout -b main
echo tip > "$repo/content"
git -C "$repo" commit -am tip
tip=$(git -C "$repo" rev-parse HEAD)

trace=$TEST_ROOT/git-trace.jsonl
selectTree='tree: { inherit (tree) rev narHash; path = tree.outPath; }'
for shallow in false true; do
    export XDG_CACHE_HOME="$TEST_ROOT/cache-$shallow"
    expr="builtins.fetchTree { type = \"git\"; url = \"file://$repo\"; rev = \"$rev\"; shallow = $shallow; }"
    # Fetch the exact revision without discovering the remote's default branch.
    : > "$trace"
    cold=$(GIT_TRACE2_EVENT="$trace" nix eval --json --apply "$selectTree" --expr "$expr")
    [[ $(jq -r .rev <<< "$cold") = "$rev" ]]
    [[ $(cat "$(jq -r .path <<< "$cold")/content") = pinned ]]
    jq -e -s 'any(.[]; .event == "cmd_name" and .name == "fetch")' "$trace"
    jq -e -s 'all(.[]; .event != "cmd_name" or (.name != "ls-remote" and .name != "symbolic-ref"))' "$trace"
    # The cache's initial symbolic HEAD must remain untouched.
    [[ $(cat "$XDG_CACHE_HOME"/nix/gitv3/*/HEAD) = 'ref: refs/heads/master' ]]

    # Expire the cache TTL and hide the remote: the cached revision must still work.
    mv "$repo" "$repo-offline"
    : > "$trace"
    warm=$(GIT_TRACE2_EVENT="$trace" nix eval --tarball-ttl 0 --json --apply "$selectTree" --expr "$expr")
    [[ $cold = "$warm" ]]
    jq -e -s 'all(.[]; .event != "cmd_name" or (.name != "fetch" and .name != "ls-remote"))' "$trace"
    mv "$repo-offline" "$repo"

    # An unpinned fetch must discover main, not use the cache's initial master HEAD.
    : > "$trace"
    unpinned=$(GIT_TRACE2_EVENT="$trace" nix eval --impure --json --apply "$selectTree" --expr "builtins.fetchTree { type = \"git\"; url = \"file://$repo\"; shallow = $shallow; }")
    [[ $(jq -r .rev <<< "$unpinned") = "$tip" ]]
    [[ $(cat "$(jq -r .path <<< "$unpinned")/content") = tip ]]
    jq -e -s 'any(.[]; .event == "cmd_name" and .name == "ls-remote")' "$trace"
    [[ $(cat "$XDG_CACHE_HOME"/nix/gitv3/*/HEAD) = 'ref: refs/heads/main' ]]

done

# allRefs populates master, so it must also discover the remote's actual default
# branch. Otherwise a later unpinned fetch could select master's older revision.
for shallow in false true; do
    export XDG_CACHE_HOME="$TEST_ROOT/cache-all-refs-$shallow"
    : > "$trace"
    result=$(GIT_TRACE2_EVENT="$trace" nix eval --json --apply "$selectTree" --expr "builtins.fetchTree { type = \"git\"; url = \"file://$repo\"; rev = \"$rev\"; allRefs = true; shallow = $shallow; }")
    [[ $(jq -r .rev <<< "$result") = "$rev" ]]
    jq -e -s 'any(.[]; .event == "cmd_name" and .name == "ls-remote")' "$trace"
    [[ $(cat "$XDG_CACHE_HOME"/nix/gitv3/*/HEAD) = 'ref: refs/heads/main' ]]
    cacheRepos=("$XDG_CACHE_HOME"/nix/gitv3/*/)
    [[ $(git --git-dir="${cacheRepos[0]}" rev-parse refs/heads/master) = "$rev" ]]
    result=$(nix eval --impure --tarball-ttl 3600 --json --apply "$selectTree" --expr "builtins.fetchTree { type = \"git\"; url = \"file://$repo\"; shallow = $shallow; }")
    [[ $(jq -r .rev <<< "$result") = "$tip" ]]
    [[ $(cat "$(jq -r .path <<< "$result")/content") = tip ]]
done

# A missing exact revision must fail rather than fall back to a branch tip.
expectStderr 1 nix eval --json --apply "$selectTree" --expr "builtins.fetchTree { type = \"git\"; url = \"file://$repo\"; rev = \"1111111111111111111111111111111111111111\"; }" > "$TEST_ROOT/missing-rev-error"

export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=protocol.file.allow
export GIT_CONFIG_VALUE_0=always

# branch = "." passes the parent's ref to each submodule. Replacing an explicit
# main ref with HEAD must preserve the pinned contents through both levels.
leaf=$TEST_ROOT/leaf
middle=$TEST_ROOT/middle
parent=$TEST_ROOT/parent
for nested in "$leaf" "$middle" "$parent"; do
    createGitRepo "$nested"
    git -C "$nested" checkout -b main
    echo pinned > "$nested/content"
    git -C "$nested" add content
    git -C "$nested" commit -m pinned
done

git -C "$middle" submodule add "file://$leaf" leaf
git -C "$middle" config -f .gitmodules submodule.leaf.branch .
git -C "$middle" add .gitmodules leaf
git -C "$middle" commit -m leaf
git -C "$parent" submodule add "file://$middle" middle
git -C "$parent" config -f .gitmodules submodule.middle.branch .
git -C "$parent" add .gitmodules middle
git -C "$parent" commit -m middle
parentRev=$(git -C "$parent" rev-parse HEAD)
# Advance every branch so accidentally fetching its tip changes the contents.
for nested in "$leaf" "$middle" "$parent"; do
    echo tip > "$nested/content"
    git -C "$nested" commit -am tip
done

expr="type = \"git\"; url = \"file://$parent\"; rev = \"$parentRev\"; submodules = true;"
concrete=$(XDG_CACHE_HOME="$TEST_ROOT/nested-concrete" nix eval --json --apply "$selectTree" --expr "builtins.fetchTree { $expr ref = \"refs/heads/main\"; }")
revOnly=$(XDG_CACHE_HOME="$TEST_ROOT/nested-rev-only" nix eval --json --apply "$selectTree" --expr "builtins.fetchTree { $expr }")
[[ $(jq -r .narHash <<< "$concrete") = "$(jq -r .narHash <<< "$revOnly")" ]]
path=$(jq -r .path <<< "$revOnly")
for file in content middle/content middle/leaf/content; do
    [[ $(cat "$path/$file") = pinned ]]
done
