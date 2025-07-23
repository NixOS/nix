#!/usr/bin/env bash

source common.sh

# Store layer needs bugfix
requireDaemonNewerThan "2.30pre20250515"

TODO_NixOS # can't enable a sandbox feature easily

enableFeatures 'recursive-nix'
restartDaemon

NIX_BIN_DIR="$(dirname "$(type -p nix)")"
export NIX_BIN_DIR

nix build -L --file ./non-trivial.nix --no-link
