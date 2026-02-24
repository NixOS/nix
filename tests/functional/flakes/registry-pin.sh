#!/usr/bin/env bash

source ./common.sh

TODO_NixOS

requireGit

clearStore
rm -rf "$TEST_HOME/.cache" "$TEST_HOME/.config"

createFlake1

# Add a branch in flake1.
git -C "$flake1Dir" checkout -b branch1
echo > "$flake1Dir/some-file"
git -C "$flake1Dir" add "$flake1Dir/some-file"
git -C "$flake1Dir" commit -m 'Some change'
git -C "$flake1Dir" checkout master

nix registry add --registry "$registry" flake1 "git+file://$flake1Dir"

commit=$(nix flake metadata "git+file://$flake1Dir" --json | jq -r '.revision')
commit2=$(nix flake metadata "git+file://$flake1Dir?ref=refs/heads/branch1" --json | jq -r '.revision')
[[ "$commit" != "$commit2" ]]

nix registry list | grepQuiet '^global' # global flake1

commit=$(nix flake metadata flake1 --json | jq -r '.revision')
commit2=$(nix flake metadata flake1/branch1 --json | jq -r '.revision')
nix build -o "$TEST_ROOT/result" flake1#root
find "$TEST_ROOT/result/" | grepInverse some-file
nix build -o "$TEST_ROOT/result" flake1/branch1#root
find "$TEST_ROOT/result/" | grepQuiet some-file
[[ "$commit" != "$commit2" ]]

nix registry pin flake1
# new output something like:
# user   flake:flake1 git+file:///tmp/nix-test/flakes/registry-pin/flake1?ref=refs/heads/master&rev=c55c61f18fa23762b1dc700af6f33af012ec6772
# global flake:flake1 git+file:///tmp/nix-test/flakes/registry-pin/flake1
nix registry list | grepQuiet '^global' # global flake1
nix registry list | grepQuiet '^user' # user flake1

nix build -o "$TEST_ROOT/result" flake1#root
find "$TEST_ROOT/result/" | grepInverse some-file
nix build -o "$TEST_ROOT/result" flake1/branch1#root
find "$TEST_ROOT/result/" | grepQuiet some-file

commit=$(nix flake metadata flake1 --json | jq -r '.revision')
# overriding the branch does still work
commit2=$(nix flake metadata flake1/branch1 --json | jq -r '.revision')
[[ "$commit" != "$commit2" ]]

nix registry remove flake1
nix registry list | grepInverse '^user' # no more pinned flake1
nix registry list | grepQuiet '^global' # global flake1 is still there
