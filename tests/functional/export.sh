#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

outPath=$(nix-build dependencies.nix --no-out-link)

nix-store --export $outPath > $TEST_ROOT/exp
nix nario export --format 1 "$outPath" > $TEST_ROOT/exp2
cmp "$TEST_ROOT/exp" "$TEST_ROOT/exp2"

nix-store --export $(nix-store -qR $outPath) > $TEST_ROOT/exp_all

nix nario export --format 1 -r "$outPath" > $TEST_ROOT/exp_all2
cmp "$TEST_ROOT/exp_all" "$TEST_ROOT/exp_all2"

if nix-store --export $outPath >/dev/full ; then
    echo "exporting to a bad file descriptor should fail"
    exit 1
fi


clearStore

if nix-store --import < $TEST_ROOT/exp; then
    echo "importing a non-closure should fail"
    exit 1
fi


clearStore

nix-store --import < $TEST_ROOT/exp_all

nix-store --export $(nix-store -qR $outPath) > $TEST_ROOT/exp_all2


clearStore

# Regression test: the derivers in exp_all2 are empty, which shouldn't
# cause a failure.
nix-store --import < $TEST_ROOT/exp_all2


# Test `nix nario import` on files created by `nix-store --export`.
clearStore
nix nario import < $TEST_ROOT/exp_all
nix path-info "$outPath"


# Test `nix nario list`.
nix nario list < $TEST_ROOT/exp_all | grepQuiet "dependencies-input-0: .* bytes"
