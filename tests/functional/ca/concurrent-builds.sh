#!/usr/bin/env bash

# Ensure that we can’t build twice the same derivation concurrently.
# Regression test for https://github.com/NixOS/nix/issues/5029

source common.sh

buggyNeedLocalStore "For some reason, this deadlocks with the daemon"

export NIX_TESTS_CA_BY_DEFAULT=1

clearStore

for i in {0..5}; do
    nix build --no-link --file ./racy.nix &
done

wait
