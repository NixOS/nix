#! /usr/bin/env bash

# Test that inputs are substituted if they cannot be fetched from their original location.

source ./common.sh

if [[ $(nix config show lazy-trees) = true ]]; then
    exit 0
fi

TODO_NixOS

createFlake1
createFlake2

nix build --no-link "$flake2Dir#bar"

path1="$(nix flake metadata --json "$flake1Dir" | jq -r .path)"

# Building after an input disappeared should succeed, because it's still in the Nix store.
mv "$flake1Dir" "$flake1Dir-tmp"
nix build --no-link "$flake2Dir#bar" --no-eval-cache

# Check that Nix will fall back to fetching the input from a substituter.
cache="file://$TEST_ROOT/binary-cache"
nix copy --to "$cache" "$path1"
clearStore
nix build --no-link "$flake2Dir#bar" --no-eval-cache --substitute --substituters "$cache"

clearStore
expectStderr 1 nix build --no-link "$flake2Dir#bar" --no-eval-cache | grepQuiet "The path.*does not exist"
