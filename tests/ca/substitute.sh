#!/usr/bin/env bash

# Ensure that binary substitution works properly with ca derivations

source common.sh

needLocalStore "“--no-require-sigs” can’t be used with the daemon"

rm -rf $TEST_ROOT/binary_cache

export REMOTE_STORE_DIR=$TEST_ROOT/binary_cache
export REMOTE_STORE=file://$REMOTE_STORE_DIR

buildDrvs () {
    nix build --file ./content-addressed.nix -L --no-link "$@"
}

# Populate the remote cache
clearStore
nix copy --to $REMOTE_STORE --file ./content-addressed.nix

# Restart the build on an empty store, ensuring that we don't build
clearStore
buildDrvs --substitute --substituters $REMOTE_STORE --no-require-sigs -j0 transitivelyDependentCA
# Check that the thing we’ve just substituted has its realisation stored
nix realisation info --file ./content-addressed.nix transitivelyDependentCA
# Check that its dependencies have it too
nix realisation info --file ./content-addressed.nix dependentCA rootCA

# Same thing, but
# 1. With non-ca derivations
# 2. Erasing the realisations on the remote store
#
# Even in that case, realising the derivations should still produce the right
# realisations on the local store
#
# Regression test for #4725
clearStore
nix build --file ../simple.nix -L --no-link --post-build-hook ../push-to-store.sh
clearStore
rm -r "$REMOTE_STORE_DIR/realisations"
nix build --file ../simple.nix -L --no-link --substitute --substituters "$REMOTE_STORE" --no-require-sigs -j0
# There's no easy way to check whether a realisation is present on the local
# store − short of manually querying the db, but the build environment doesn't
# have the sqlite binary − so we instead push things again, and check that the
# realisations have correctly been pushed to the remote store
nix copy --to "$REMOTE_STORE" --file ../simple.nix
if [[ -z "$(ls "$REMOTE_STORE_DIR/realisations")" ]]; then
    echo "Realisations not rebuilt"
    exit 1
fi

# Test the local realisation disk cache
buildDrvs --post-build-hook ../push-to-store.sh
clearStore
# Add the realisations of rootCA to the cachecache
clearCacheCache
export _NIX_FORCE_HTTP=1
buildDrvs --substitute --substituters $REMOTE_STORE --no-require-sigs -j0
# Try rebuilding, but remove the realisations from the remote cache to force
# using the cachecache
clearStore
rm $REMOTE_STORE_DIR/realisations/*
buildDrvs --substitute --substituters $REMOTE_STORE --no-require-sigs -j0
