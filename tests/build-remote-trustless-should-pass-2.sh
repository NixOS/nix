source common.sh

# Remote doesn't trust us nor do we think it does
file=build-hook.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng
trusting=false

source build-remote-trustless.sh
