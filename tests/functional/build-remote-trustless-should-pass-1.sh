source common.sh

# Remote trusts us
file=build-hook.nix
prog=nix-daemon
proto=ssh-ng

source build-remote-trustless.sh
source build-remote-trustless-after.sh
