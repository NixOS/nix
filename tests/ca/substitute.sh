#!/usr/bin/env bash

# Ensure that binary substitution works properly with ca derivations

source common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references/' "$NIX_CONF_DIR"/nix.conf

rm -rf $TEST_ROOT/binary_cache

export REMOTE_STORE=file://$TEST_ROOT/binary_cache

buildDrvs () {
    nix build --file ./content-addressed.nix -L --no-link "$@"
}

# Populate the remote cache
clearStore
buildDrvs --post-build-hook ../push-to-store.sh

# Restart the build on an empty store, ensuring that we don't build
clearStore
buildDrvs --substitute --substituters $REMOTE_STORE --no-require-sigs -j0

