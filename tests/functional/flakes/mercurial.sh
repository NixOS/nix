#!/usr/bin/env bash

source ./common.sh

[[ $(type -p hg) ]] || skipTest "Mercurial not installed"

flake1Dir=$TEST_ROOT/flake-hg1
mkdir -p "$flake1Dir"
writeSimpleFlake "$flake1Dir"
hg init "$flake1Dir"

nix registry add --registry "$registry" flake1 "hg+file://$flake1Dir"

flake2Dir=$TEST_ROOT/flake-hg2
mkdir -p "$flake2Dir"
writeDependentFlake "$flake2Dir"
hg init "$flake2Dir"

hg add "$flake1Dir"/*
hg commit --config ui.username=foobar@example.org "$flake1Dir" -m 'Initial commit'

hg add "$flake2Dir"/flake.nix
hg commit --config ui.username=foobar@example.org "$flake2Dir" -m 'Initial commit'

nix build -o "$TEST_ROOT/result" "hg+file://$flake2Dir"
[[ -e $TEST_ROOT/result/hello ]]

(! nix flake metadata --json "hg+file://$flake2Dir" | jq -e -r .revision)

_NIX_TEST_BARF_ON_UNCACHEABLE='' nix eval "hg+file://$flake2Dir"#expr

_NIX_TEST_BARF_ON_UNCACHEABLE='' nix eval "hg+file://$flake2Dir"#expr

(! nix eval "hg+file://$flake2Dir"#expr --no-allow-dirty)

(! nix flake metadata --json "hg+file://$flake2Dir" | jq -e -r .revision)

hg commit --config ui.username=foobar@example.org "$flake2Dir" -m 'Add lock file'

nix flake metadata --json "hg+file://$flake2Dir" --refresh | jq -e -r .revision
nix flake metadata --json "hg+file://$flake2Dir"
[[ $(nix flake metadata --json "hg+file://$flake2Dir" | jq -e -r .revCount) = 1 ]]

nix build -o "$TEST_ROOT/result" "hg+file://$flake2Dir" --no-registries --no-allow-dirty
nix build -o "$TEST_ROOT/result" "hg+file://$flake2Dir" --no-use-registries --no-allow-dirty
