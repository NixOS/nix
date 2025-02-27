#!/usr/bin/env bash

source common.sh

# Store layer needs bugfix
requireDaemonNewerThan "2.27pre20250205"

TODO_NixOS # can't enable a sandbox feature easily

enableFeatures 'recursive-nix'
restartDaemon

NIX_BIN_DIR="$(dirname "$(type -p nix)")"
export NIX_BIN_DIR

expectStderr 1 nix build -L --file ./non-trivial.nix --no-link | grepQuiet "Building dynamic derivations in one shot is not yet implemented"
