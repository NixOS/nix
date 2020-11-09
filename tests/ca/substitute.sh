#!/usr/bin/env bash

# Ensure that binary substitution works properly with ca derivations

source common.sh

export REMOTE_STORE=file://$TEST_ROOT/binary_cache

buildDrvs () {
    nix --experimental-features 'nix-command ca-derivations ca-references' build --file ./content-addressed.nix -L --no-link "$@"
}

# Populate the remote cache
buildDrvs --post-build-hook ../push-to-store.sh

# Restart the build on an empty store, ensuring that we don't build
clearStore
buildDrvs --substitute --substituters $REMOTE_STORE --no-require-sigs|& \
    (! grep -q Building)

