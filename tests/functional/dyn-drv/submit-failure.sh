#!/usr/bin/env bash

source common.sh

# builder-rpc-v0
requireDaemonNewerThan "2.35pre20260507"

TODO_NixOS

enableFeatures 'dynamic-derivations ca-derivations'
restartDaemon

NIX_BIN_DIR="$(dirname "$(type -p nix)")"
export NIX_BIN_DIR

expectStderr 1 nix build -L --file ./submit-failure.nix noSubmit --no-link | grepQuiet "failed to submit output"
expectStderr 1 nix build -L --file ./submit-failure.nix duplicate --no-link | grepQuiet "submit duplicate output"
