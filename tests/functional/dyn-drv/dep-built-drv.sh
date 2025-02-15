#!/usr/bin/env bash

source common.sh

out1=$(nix-build ./text-hashed-output.nix -A hello --no-out-link)

# Store layer needs bugfix
requireDaemonNewerThan "2.27pre20250205"

clearStore

out2=$(nix-build ./text-hashed-output.nix -A wrapper --no-out-link)

diff -r $out1 $out2
