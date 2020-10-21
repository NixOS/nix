#!/usr/bin/env bash

source common.sh

clearStore
clearCache

commonFlags=(--experimental-features 'ca-derivations ca-references nix-command')

drvPath=$(nix-instantiate "${commonFlags[@]}" ./content-addressed.nix -A transitivelyDependentCA --arg seed 1)
nix copy "${commonFlags[@]}" --to file://$cacheDir $drvPath\!out

clearStore
nix copy "${commonFlags[@]}" --from file://$cacheDir $drvPath\!out --no-require-sigs
