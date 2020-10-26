#!/usr/bin/env bash

source common.sh

clearStore
clearCache
clearCache

export REMOTE_STORE=file://$cacheDir

commonFlags=( \
    ./content-addressed.nix -A transitivelyDependentCA \
    --arg seed 1 \
    --experimental-features 'ca-derivations ca-references nix-command' \
    --no-out-link \
)

nix-build "${commonFlags[@]}" --post-build-hook $PWD/push-to-store.sh

clearStore
# XXX: The `grep` call is a work around the fact that `-j0` currently doesn't
# take the possibility that drvs might be resolved into account, so would fail
# because Nix can't know *a priori* that everything is already in the binary
# cache
nix-build "${commonFlags[@]}" --no-require-sigs --substituters file://$cacheDir |& \
    (! grep -q 'Building a')
