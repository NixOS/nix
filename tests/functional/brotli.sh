#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore
clearCache

cacheURI="file://$cacheDir?compression=br"

outPath=$(nix-build dependencies.nix --no-out-link)

nix copy --to "$cacheURI" "$outPath"

HASH=$(nix hash path "$outPath")

clearStore
clearCacheCache

nix copy --from "$cacheURI" "$outPath" --no-check-sigs

HASH2=$(nix hash path "$outPath")

[[ $HASH == "$HASH2" ]]
