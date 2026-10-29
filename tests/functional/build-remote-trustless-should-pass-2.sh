#!/usr/bin/env bash

source common.sh

enableFeatures "daemon-trust-override"

TODO_NixOS

restartDaemon

# Remote doesn't trust us
file=build-hook.nix
prog=$(readlink -e ./common/nix2-daemon-untrusting.sh)
proto=ssh-ng

source build-remote-trustless.sh
source build-remote-trustless-after.sh
