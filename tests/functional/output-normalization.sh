#!/usr/bin/env bash

source common.sh

testNormalization () {
    TODO_NixOS
    clearStore
    outPath=$(nix-build ./simple.nix --no-out-link)
    test "$(stat -c %Y "$outPath")" -eq 1
}

testNormalization
