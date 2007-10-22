#! /bin/sh

initctl stop nix-daemon
killproc.sh nix-worker
sleep 2
initctl start nix-daemon
