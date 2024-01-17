source common.sh

enableFeatures "daemon-trust-override"

restartDaemon

# Remote doesn't trusts us, but this is fine because we are only
# building (fixed) CA derivations.
file=build-hook-ca-fixed.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng

source build-remote-trustless.sh
source build-remote-trustless-after.sh
