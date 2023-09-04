source ./common.sh

requireDaemonNewerThan "2.4pre20210625"

sed -i 's/experimental-features .*/& ca-derivations ca-references/' "$NIX_CONF_DIR"/nix.conf

export REMOTE_STORE_DIR="$TEST_ROOT/remote_store"
export REMOTE_STORE="file://$REMOTE_STORE_DIR"

rm -rf $REMOTE_STORE_DIR
clearStore

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
nix build --substituters "$REMOTE_STORE" -f nondeterministic.nix toplevel --no-require-sigs --no-link
