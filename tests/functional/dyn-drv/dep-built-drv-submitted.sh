#!/usr/bin/env bash

source common.sh

# builder-rpc-v0
requireDaemonNewerThan "2.35pre20260507"

TODO_NixOS # can't enable a sandbox feature easily

enableFeatures 'ca-derivations'
restartDaemon

NIX_BIN_DIR="$(dirname "$(type -p nix)")"
export NIX_BIN_DIR

nix build -L --file ./non-trivial-submitted.nix unstructured --no-link
nix build -L --file ./non-trivial-submitted.nix structured --no-link
