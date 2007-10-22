#! /bin/sh

initctl stop nix-daemon
killproc.sh nix-worker
sleep 2

#/nixstate2/nix/bin/nix-worker --daemon > /dev/null 2>&1 &
/nixstate2/nix/bin/nix-worker --daemon
#gdb --args /nixstate2/nix/bin/nix-worker --daemon
