source common.sh

clearStore

rm -f $TEST_ROOT/result

export REMOTE_STORE=file:$TEST_ROOT/remote_store
echo 'require-sigs = false' >> $NIX_CONF_DIR/nix.conf

restartDaemon

# Build the dependencies and push them to the remote store
nix-build -o $TEST_ROOT/result dependencies.nix --post-build-hook $PWD/push-to-store.sh

clearStore

# Ensure that we the remote store contains both the runtime and buildtime
# closure of what we've just built
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix input1_drv
