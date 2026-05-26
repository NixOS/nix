#!/usr/bin/env bash

# Regression test for incomplete closures of content-addressed derivations:
# when a binary cache is missing a transitive dependency's path (e.g. it was
# garbage-collected), Nix should build the missing dependency and substitute
# the still-cached parents, rather than rebuilding the whole closure.

source common.sh

needLocalStore "'--no-require-sigs' can’t be used with the daemon"

export REMOTE_STORE_DIR="$TEST_ROOT/binary_cache"
export REMOTE_STORE="file://$REMOTE_STORE_DIR"
rm -rf "$REMOTE_STORE_DIR"

# Build the full CA closure (transitivelyDependentCA -> dependentCA -> rootCA)
# and populate the binary cache (paths + realisations).
clearStore
clearCacheCache
nix copy --to "$REMOTE_STORE" --file ./content-addressed.nix transitivelyDependentCA

# Simulate a cache GC of the deepest dependency (rootCA): drop its narinfo and
# NAR, keeping its realisation and the parents' paths. This leaves the cache
# serving an incomplete closure of the parents.
narinfo=$(grep -l "StorePath:.*-rootCA$" "$REMOTE_STORE_DIR"/*.narinfo)
[[ -f "$narinfo" ]] || fail "setup: could not locate rootCA narinfo in $REMOTE_STORE_DIR"
url=$(awk '/^URL:/{print $2}' "$narinfo")
[[ -n "$url" && -f "$REMOTE_STORE_DIR/$url" ]] || fail "setup: rootCA narinfo had no URL or NAR missing ($url)"
rm "$narinfo" "$REMOTE_STORE_DIR/$url"

# Realise from the (incomplete) cache, allowing builds.
clearStore
clearCacheCache
nix build --file ./content-addressed.nix -L --no-link \
    --substitute --substituters "$REMOTE_STORE" --no-require-sigs \
    transitivelyDependentCA 2>&1 | tee "$TEST_ROOT/log"

# The garbage-collected dependency is rebuilt from source ...
grepQuiet "building '.*-rootCA.drv'" "$TEST_ROOT/log"
# ... which fills the hole, so the still-cached top of the closure is
# substituted rather than rebuilt.
grepQuiet "copying path '.*-transitively-dependent'" "$TEST_ROOT/log"
grepQuietInverse "building '.*-transitively-dependent.drv'" "$TEST_ROOT/log"
