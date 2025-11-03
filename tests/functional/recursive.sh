#!/usr/bin/env bash

source common.sh

TODO_NixOS # can't enable a sandbox feature easily

enableFeatures 'recursive-nix'
restartDaemon

clearStore

rm -f "$TEST_ROOT"/result

unreachable=$(nix store add-path ./recursive.sh)
export unreachable

NIX_BIN_DIR=$(dirname "$(type -p nix)") nix --extra-experimental-features 'nix-command recursive-nix' build -o "$TEST_ROOT"/result -L --impure --file ./recursive.nix

[[ $(cat "$TEST_ROOT"/result/inner1) =~ blaat ]]

# Make sure the recursively created paths are in the closure.
nix path-info -r "$TEST_ROOT"/result | grep foobar
nix path-info -r "$TEST_ROOT"/result | grep fnord
nix path-info -r "$TEST_ROOT"/result | grep inner1
