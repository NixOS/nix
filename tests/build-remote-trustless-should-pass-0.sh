source common.sh

# Remote trusts us but we pretend it doesn't.
file=build-hook.nix
prog=nix-store
proto=ssh
trusting=false

source build-remote-trustless.sh
