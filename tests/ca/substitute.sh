#!/usr/bin/env bash

# Ensure that binary substitution works properly with ca derivations

source common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references/' "$NIX_CONF_DIR"/nix.conf

rm -rf $TEST_ROOT/binary_cache

export REMOTE_STORE_DIR=$TEST_ROOT/binary_cache
export REMOTE_STORE=file://$REMOTE_STORE_DIR

buildDrvs () {
    nix build --file ./content-addressed.nix -L --no-link "$@"
}

# Populate the remote cache
clearStore
buildDrvs --post-build-hook ../push-to-store.sh

# Restart the build on an empty store, ensuring that we don't build
clearStore
buildDrvs --substitute --substituters $REMOTE_STORE --no-require-sigs -j0

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
