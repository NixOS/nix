#!/usr/bin/env bash

source ./common.sh

requireGit

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2

createGitRepo "$flake1Dir"
cat > "$flake1Dir"/flake.nix <<EOF
{
    outputs = { self }: { x = import ./x.nix; };
}
EOF
echo 123 > "$flake1Dir"/x.nix
git -C "$flake1Dir" add flake.nix x.nix
git -C "$flake1Dir" commit -m Initial

createGitRepo "$flake2Dir"
cat > "$flake2Dir"/flake.nix <<EOF
{
    outputs = { self, flake1 }: { x = flake1.x; };
}
EOF
git -C "$flake2Dir" add flake.nix

[[ $(nix eval --json "$flake2Dir#x" --override-input flake1 "$TEST_ROOT/flake1") = 123 ]]

echo 456 > "$flake1Dir"/x.nix

[[ $(nix eval --json "$flake2Dir#x" --override-input flake1 "$TEST_ROOT/flake1") = 456 ]]

# Dirty overrides require --allow-dirty-locks.
expectStderr 1 nix flake lock "$flake2Dir" --override-input flake1 "$TEST_ROOT/flake1" |
  grepQuiet "Not writing lock file.*because it has an unlocked input"

nix flake lock "$flake2Dir" --override-input flake1 "$TEST_ROOT/flake1" --allow-dirty-locks

# Using a lock file with a dirty lock does not require --allow-dirty-locks, but should print a warning.
expectStderr 0 nix eval "$flake2Dir#x" |
  grepQuiet "warning: Lock file entry .* is unlocked"

[[ $(nix eval "$flake2Dir#x") = 456 ]]
