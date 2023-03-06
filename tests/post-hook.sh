source common.sh

# TODO debug failure with 2.3
requireDaemonVersionAtleast "2.4pre"

clearStore

rm -f $TEST_ROOT/result

export REMOTE_STORE=file://$TEST_ROOT/remote_store
echo 'require-sigs = false' >> $NIX_CONF_DIR/nix.conf

restartDaemon

if isDaemonNewer "2.13"; then
    pushToStore="$PWD/push-to-store.sh"
else
    pushToStore="$PWD/push-to-store-old.sh"
fi

# Build the dependencies and push them to the remote store.
nix-build -o $TEST_ROOT/result dependencies.nix --post-build-hook "$pushToStore"

clearStore

# Ensure that the remote store contains both the runtime and build-time
# closure of what we've just built.
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix input1_drv
