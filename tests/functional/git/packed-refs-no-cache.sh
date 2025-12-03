#!/usr/bin/env bash

# Please see https://github.com/NixOS/nix/issues/13457
# for a higher description of the purpose of the test.
# tl;dr;fetchGit will utilize the git cache and avoid refetching when possible.
# It relies on the presence of either the commit when rev is provided
# or checks if the ref refs/heads/<ref_name> if ref is provided.
#
# Unfortunately, git can occasionally "pack references" which moves the references
# from individual files to a single unifies file.
# When this occurs, nix can no longer check for the presence of the ref to check
# for the mtime and will refetch unnecessarily.

source ../common.sh

requireGit

clearStoreIfPossible

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./git

export _NIX_FORCE_HTTP=1

rm -rf "$repo" "${repo}-tmp" "$TEST_HOME/.cache/nix"

git init --initial-branch="master" "$repo"
git -C "$repo" config user.email "nix-tests@example.com"
git -C "$repo" config user.name "Nix Tests"

echo "hello world" > "$repo/hello_world"
git -C "$repo" add hello_world
git -C "$repo" commit -m 'My first commit.'

# We now do an eval
nix eval --impure --raw --expr "builtins.fetchGit { url = file://$repo; }"

# test that our eval even worked by checking for the presence of the file
[[ $(nix eval --impure --raw --expr "builtins.readFile ((builtins.fetchGit { url = file://$repo; }) + \"/hello_world\")") = 'hello world' ]]

# Validate that refs/heads/master exists
shopt -s nullglob
matches=("$TEST_HOME/.cache/nix/gitv3/*/refs/heads/master")
shopt -u nullglob

if [[ ${#matches[@]} -eq 0 ]]; then
  echo "refs/heads/master does not exist."
  exit 1
fi
# pack refs
git -C "$TEST_HOME"/.cache/nix/gitv3/*/ pack-refs --all

shopt -s nullglob
matches=("$TEST_HOME"/.cache/nix/gitv3/*/refs/heads/master)
shopt -u nullglob

# ensure refs/heads/master is now gone
if [[ ${#matches[@]} -ne 0 ]]; then
  echo "refs/heads/master still exists after pack-refs"
  exit 1
fi

# create a new commit
echo "hello again" > "$repo/hello_again"
git -C "$repo" add hello_again
git -C "$repo" commit -m 'Second commit.'

# re-eval — this should return the path to the cached version
store_path=$(nix eval --tarball-ttl 3600 --impure --raw --expr "(builtins.fetchGit { url = file://$repo; }).outPath")
echo "Fetched store path: $store_path"

# Validate that the new file is *not* there
# FIXME: This is a broken test case and we should swap the assertion here.
if [[ -e "$store_path/hello_again" ]]; then
  echo "ERROR: Cached fetchGit should not include the new commit."
  exit 0
else
  echo "PASS: New commit was not fetched due to caching (as expected)."
  exit 1
fi
