#!/usr/bin/env bash

source common.sh

out1=$(nix-build ./text-hashed-output.nix -A hello --no-out-link)

clearStore

expectStderr 1 nix-build ./text-hashed-output.nix -A wrapper --no-out-link | grepQuiet "Building dynamic derivations in one shot is not yet implemented"

# diff -r $out1 $out2
