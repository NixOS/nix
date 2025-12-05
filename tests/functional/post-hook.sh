#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

rm -f "$TEST_ROOT"/result

export REMOTE_STORE=file:$TEST_ROOT/remote_store
echo 'require-sigs = false' >> "$test_nix_conf"

restartDaemon

if isDaemonNewer "2.13"; then
    pushToStore="$PWD/push-to-store.sh"
else
    pushToStore="$PWD/push-to-store-old.sh"
fi

# Build the dependencies and push them to the remote store.
nix-build -o "$TEST_ROOT"/result dependencies.nix --post-build-hook "$pushToStore"
# See if all outputs are passed to the post-build hook by only specifying one
# We're not able to test CA tests this way
#
# FIXME: This export is hiding error condition
# shellcheck disable=SC2155
export BUILD_HOOK_ONLY_OUT_PATHS=$([ ! "$NIX_TESTS_CA_BY_DEFAULT" ])
nix-build -o "$TEST_ROOT"/result-mult multiple-outputs.nix -A a.first --post-build-hook "$pushToStore"

if isDaemonNewer "2.33.0pre20251029"; then
    # Regression test for issue #14287: `--check` should re-run post build
    # hook, even though nothing is getting newly registered.
    export HOOK_DEST=$TEST_ROOT/listing
    # Needed so the hook will get the above environment variable.
    restartDaemon
    nix-build -o "$TEST_ROOT"/result-mult multiple-outputs.nix --check -A a.first --post-build-hook "$PWD/build-hook-list-paths.sh"
    grepQuiet a-first "$HOOK_DEST"
    grepQuiet a-second "$HOOK_DEST"
    unset HOOK_DEST
fi

clearStore

# Ensure that the remote store contains both the runtime and build-time
# closure of what we've just built.
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix input1_drv
nix copy --from "$REMOTE_STORE" --no-require-sigs -f multiple-outputs.nix a^second
