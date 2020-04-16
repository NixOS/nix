#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

out1=$(nix-build ./content-addressed.nix -A dependent --arg seed 1 -vvv)
out2=$(nix-build ./content-addressed.nix -A dependent --arg seed 2 -vvv)

test $out1 == $out2

# nix-build ./content-addressed.nix --arg seed 3 |& (! grep -q "building transitively-dependent")

# clearStore

# nix-build ./content-addressed.nix --arg seed 1 \
#   --substituters "file://$cacheDir" \
#   --no-require-sigs \
#   --max-jobs 0
