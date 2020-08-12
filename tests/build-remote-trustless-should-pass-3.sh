source common.sh

# We act as if remote trusts us, but it doesn't. This is fine because we
# are only building (fixed) CA derivations.
file=build-hook-ca.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng
trusting=true

source build-remote-trustless.sh
