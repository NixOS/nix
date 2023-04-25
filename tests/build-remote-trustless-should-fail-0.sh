source common.sh

enableFeatures "daemon-trust-override"

restartDaemon

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

# Now when we go to build that downstream derivation, Nix will fail
# because we cannot trustlessly build input-addressed derivations with
# `inputDrv` dependencies.

file=build-hook.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng

expectStderr 1 source build-remote-trustless.sh \
    | grepQuiet "you are not privileged to build input-addressed derivations"
