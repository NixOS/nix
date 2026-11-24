#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.37pre"

TODO_NixOS

enableFeatures 'dynamic-derivations ca-derivations'
restartDaemon

NIX_BIN_DIR="$(dirname "$(type -p nix)")"
export NIX_BIN_DIR

outPath="$(nix build -L --file ./submit-instantiate.nix result --no-link --print-out-paths)"

[[ "$(cat "$outPath")" == $'dep\nmain' ]]
