#!/usr/bin/env bash

source ./common.sh

createFlake1

export EDITOR=cat
nix edit "$flake1Dir#" | grepQuiet simple.builder.sh
tar --exclude=".git*" -czf "$TEST_ROOT"/flake1Dir.tar.gz -C "$(dirname "$flake1Dir")" "$(basename "$flake1Dir")"
# Test that editing a file from a tarball flake works and the file is readonly.
nix edit "file://$TEST_ROOT/flake1Dir.tar.gz" | grepQuiet simple.builder.sh
EDITOR='test ! -w' nix edit "file://$TEST_ROOT/flake1Dir.tar.gz"
