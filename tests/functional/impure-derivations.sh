#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.8pre20220311"

TODO_NixOS

enableFeatures "ca-derivations impure-derivations"
restartDaemon

clearStoreIfPossible

# Basic test of impure derivations: building one a second time should not use the previous result.
printf 0 > "$TEST_ROOT"/counter

# `nix derivation add` with impure derivations work
drvPath=$(nix-instantiate ./impure-derivations.nix -A impure)
nix derivation show "$drvPath" | jq .[] > "$TEST_HOME"/impure-drv.json
drvPath2=$(nix derivation add < "$TEST_HOME"/impure-drv.json)
[[ "$drvPath" = "$drvPath2" ]]

# But only with the experimental feature!
expectStderr 1 nix derivation add < "$TEST_HOME"/impure-drv.json --experimental-features nix-command | grepQuiet "experimental Nix feature 'impure-derivations' is disabled"

nix build --dry-run --json --file ./impure-derivations.nix impure.all
json=$(nix build -L --no-link --json --file ./impure-derivations.nix impure.all)
path1=$(echo "$json" | jq -r .[].outputs.out)
path1_stuff=$(echo "$json" | jq -r .[].outputs.stuff)
[[ $(< "$path1"/n) = 0 ]]
[[ $(< "$path1_stuff"/bla) = 0 ]]

nix path-info --json "$path1" | jq -e '.[].ca | .method == "nar" and .hash.algorithm == "sha256"'

path2=$(nix build -L --no-link --json --file ./impure-derivations.nix impure | jq -r .[].outputs.out)
[[ $(< "$path2"/n) = 1 ]]

# Test impure derivations that depend on impure derivations.
path3=$(nix build -L --no-link --json --file ./impure-derivations.nix impureOnImpure | jq -r .[].outputs.out)
[[ $(< "$path3"/n) = X2 ]]

path4=$(nix build -L --no-link --json --file ./impure-derivations.nix impureOnImpure | jq -r .[].outputs.out)
[[ $(< "$path4"/n) = X3 ]]

# Test that (self-)references work.
[[ $(< "$path4"/symlink/bla) = 3 ]]
[[ $(< "$path4"/self/n) = X3 ]]

# Input-addressed derivations cannot depend on impure derivations directly.
(! nix build -L --no-link --json --file ./impure-derivations.nix inputAddressed 2>&1) | grep 'depends on impure derivation'

drvPath=$(nix eval --json --file ./impure-derivations.nix impure.drvPath | jq -r .)
[[ $(nix derivation show "$drvPath" | jq ".[\"$(basename "$drvPath")\"].outputs.out.impure") = true ]]
[[ $(nix derivation show "$drvPath" | jq ".[\"$(basename "$drvPath")\"].outputs.stuff.impure") = true ]]

# Fixed-output derivations *can* depend on impure derivations.
path5=$(nix build -L --no-link --json --file ./impure-derivations.nix contentAddressed | jq -r .[].outputs.out)
[[ $(< "$path5") = X ]]
[[ $(< "$TEST_ROOT"/counter) = 5 ]]

# And they should not be rebuilt.
path5=$(nix build -L --no-link --json --file ./impure-derivations.nix contentAddressed | jq -r .[].outputs.out)
[[ $(< "$path5") = X ]]
[[ $(< "$TEST_ROOT"/counter) = 5 ]]

# Input-addressed derivations can depend on fixed-output derivations that depend on impure derivations.
path6=$(nix build -L --no-link --json --file ./impure-derivations.nix inputAddressedAfterCA | jq -r .[].outputs.out)
[[ $(< "$path6") = X ]]
[[ $(< "$TEST_ROOT"/counter) = 5 ]]

# Test nix/fetchurl.nix.
path7=$(nix build -L --no-link --print-out-paths --expr "import <nix/fetchurl.nix> { impure = true; url = file://$PWD/impure-derivations.sh; }")
cmp "$path7" "$PWD"/impure-derivations.sh
