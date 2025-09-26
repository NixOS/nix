#!/usr/bin/env bash

source ./common.sh

requireGit

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2

createGitRepo "$flake1Dir"
cat > "$flake1Dir"/flake.nix <<EOF
{
    outputs = { self }: { x = builtins.readFile $(pwd)/absolute-paths.sh; };
}
EOF
git -C "$flake1Dir" add flake.nix
git -C "$flake1Dir" commit -m Initial

nix eval --impure --json "$flake1Dir"#x
