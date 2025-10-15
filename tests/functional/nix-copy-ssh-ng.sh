#!/usr/bin/env bash

source common.sh

source nix-copy-ssh-common.sh "ssh-ng"

TODO_NixOS

clearStore
clearRemoteStore

outPath=$(nix-build --no-out-link dependencies.nix)

nix store info --store "$remoteStore"

# Regression test for https://github.com/NixOS/nix/issues/6253
nix copy --to "$remoteStore" "$outPath" --no-check-sigs &
nix copy --to "$remoteStore" "$outPath" --no-check-sigs
