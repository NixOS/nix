#!/usr/bin/env bash

source common.sh

path=$(nix build --no-link --print-out-paths -f simple.nix)

hash_part=$(basename "$path")
hash_part=${hash_part:0:32}

path2=$(nix store path-from-hash-part "$hash_part")

[[ $path = "$path2" ]]
