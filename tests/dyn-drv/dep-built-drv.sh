#!/usr/bin/env bash

source common.sh

feats=(--experimental-features 'nix-command ca-derivations')

out1=$(nix-build "${feats[@]}" ./text-hashed-output.nix -A root --no-out-link)

clearStore

out2=$(nix-build "${feats[@]}" ./text-hashed-output.nix -A wrapper --no-out-link)

diff -r $out1 $out2
