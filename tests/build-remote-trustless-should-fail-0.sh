source common.sh

[[ $busybox =~ busybox ]] || skipTest "no busybox"

unset NIX_STORE_DIR
unset NIX_STATE_DIR

# We first build a dependency of the derivation we eventually want to
# build.
nix-build build-hook.nix -A passthru.input2 \
  -o "$TEST_ROOT/input2" \
  --arg busybox "$busybox" \
  --store "$TEST_ROOT/local" \
  --option system-features bar

# Now when we go to build that downstream derivation, Nix will try to
# copy our already-build `input2` to the remote store. That store object
# is input-addressed, so this will fail.

file=build-hook.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng

expectStderr 1 source build-remote-trustless.sh \
    | grepQuiet "cannot add path '[^ ]*' because it lacks a signature by a trusted key"
