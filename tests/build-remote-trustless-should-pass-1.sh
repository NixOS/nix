source common.sh

# Remote trusts us but we pretend it doesn't.
file=build-hook.nix
prog=nix-daemon
proto=ssh-ng
trusting=false

source build-remote-trustless.sh
