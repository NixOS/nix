source common.sh

# We act as if remote trusts us, but it doesn't. This fails since we are
# building input-addressed derivations with `buildDerivation`, which
# depends on trust.
file=build-hook.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng
trusting=true

! source build-remote-trustless.sh
