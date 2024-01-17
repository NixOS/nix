source common.sh

# Remote trusts us
file=build-hook.nix
prog=nix-store
proto=ssh

source build-remote-trustless.sh
source build-remote-trustless-after.sh
