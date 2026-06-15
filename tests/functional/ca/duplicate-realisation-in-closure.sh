#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.4pre20210625"

export REMOTE_STORE="file://$cacheDir"

# Build dep1 and push that to the binary cache.
# This entails building (and pushing) current-time.
nix copy --to "$REMOTE_STORE" -f nondeterministic.nix dep1
clearStore
sleep 2 # To make sure that `$(date)` will be different
# Build dep2.
# As we’ve cleared the cache, we’ll have to rebuild current-time. And because
# the current time isn’t the same as before, this will yield a new (different)
# realisation
nix build -f nondeterministic.nix dep2 --no-link

# Build something that depends both on dep1 and dep2.
# If everything goes right, we should rebuild dep2 rather than fetch it from
# the cache (because that would mean duplicating `current-time` in the closure),
# and have `dep1 == dep2`.

# FIXME: Force the use of small-step resolutions only to fix this in a
# better way (#11896, #11928).
skipTest "temporarily broken because dependent realisations are removed"

nix build --substituters "$REMOTE_STORE" -f nondeterministic.nix toplevel --no-require-sigs --no-link
