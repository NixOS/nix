#!/usr/bin/env bash

source common.sh

TODO_NixOS

requireDaemonNewerThan "2.36.0pre20260716"

enableFeatures 'recursive-nix dynamic-derivations'
restartDaemon

# grep is overridden with a function in this shell, is not in a new subshell
EXTRA_PATH=$(dirname "$(type -p nix)"):$(dirname "$(sh -c 'type -p grep')")
export EXTRA_PATH

nix build -L --file ./add-scanning.nix --no-link
