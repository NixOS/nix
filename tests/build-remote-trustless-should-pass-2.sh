source common.sh

# Remote doesn't trust us
file=build-hook.nix
prog=$(readlink -e ./nix-daemon-untrusting.sh)
proto=ssh-ng

source build-remote-trustless.sh
source build-remote-trustless-after.sh
