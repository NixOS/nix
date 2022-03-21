source common.sh

file=build-hook.nix

source build-remote.sh

# Add a `post-build-hook` option to the nix conf.
# This hook will be executed both for the local machine and the remote builders
# (because they share the same config).
registerBuildHook () {
    # Dummy post-build-hook just to ensure that it's executed correctly.
    # (we can't reuse the one from `$PWD/push-to-store.sh` because of
    # https://github.com/NixOS/nix/issues/4341)
    cat <<EOF > $TEST_ROOT/post-build-hook.sh
#!/bin/sh

echo "Post hook ran successfully"
# Add an empty line to a counter file, just to check that this hook ran properly
echo "" >> $TEST_ROOT/post-hook-counter
EOF
    chmod +x $TEST_ROOT/post-build-hook.sh
    rm -f $TEST_ROOT/post-hook-counter

    echo "post-build-hook = $TEST_ROOT/post-build-hook.sh" >> $NIX_CONF_DIR/nix.conf
}

registerBuildHook
source build-remote.sh

# `build-hook.nix` has four derivations to build, and the hook runs twice for
# each derivation (once on the builder and once on the host), so the counter
# should contain eight lines now
[[ $(cat $TEST_ROOT/post-hook-counter | wc -l) -eq 8 ]]
