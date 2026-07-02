#!/usr/bin/env bash

# The purpose of this test is to ensure that the HEAD file in the git cache
# is cached correctly and it is read on subsequent fetchGit calls
# and does not change when the branch is switched or new commits are made.
# It is related to investigating https://github.com/NixOS/nix/issues/13556

source ../common.sh

requireGit

clearStoreIfPossible

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./git

export _NIX_FORCE_HTTP=1
export _NIX_TEST_ATTRS_REF=1

rm -rf "$repo" "${repo}-tmp" "$TEST_HOME/.cache/nix"

git init --initial-branch="master" "$repo"
git -C "$repo" config user.email "nix-tests@example.com"
git -C "$repo" config user.name "Nix Tests"

# make a commit on master
echo "hello world" > "$repo/hello_world"
git -C "$repo" add hello_world
git -C "$repo" commit -m 'My first commit.'
original_ref=$(git -C "$repo" symbolic-ref HEAD)
 
# We now do an eval
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$repo; }).ref") = "$original_ref" ]]

# Validate that gitv3/*/HEAD exists and its contents are original_ref
shopt -s nullglob
head_files=("$TEST_HOME/.cache/nix/gitv3/"*/HEAD)
shopt -u nullglob

if [[ ${#head_files[@]} -eq 0 ]]; then
  echo "HEAD file does not exist."
  exit 1
fi

for head_file in "${head_files[@]}"; do
  if ! grep -q "$original_ref" "$head_file"; then
    echo "HEAD file $head_file does not contain '${original_ref}'."
    exit 1
  fi
done

# create another branch with a different commit
git -C "$repo" switch -c "another-branch"
echo "good bye" > "$repo/good_bye"
git -C "$repo" add good_bye
git -C "$repo" commit -m 'My second commit.'
new_ref=$(git -C "$repo" symbolic-ref HEAD)

# assert that new_ref != original_ref
[[ "$new_ref" != "$original_ref" ]]

# We now do an eval
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit { url = file://$repo; }).ref") = "$original_ref" ]]

# The HEAD file should still be $original_ref since we should be using the cached HEAD
for head_file in "${head_files[@]}"; do
  if ! grep -q "$original_ref" "$head_file"; then
    echo "HEAD file $head_file does not contain '${original_ref}'."
    exit 1
  fi
done
