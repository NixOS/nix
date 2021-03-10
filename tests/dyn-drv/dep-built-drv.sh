#!/usr/bin/env bash

source common.sh

out1=$(nix-build ./text-hashed-output.nix -A hello --no-out-link)

clearStore

expectStderr 1 nix-build ./text-hashed-output.nix -A wrapper --no-out-link | grepQuiet "Dependencies on the outputs of dynamic derivations are not yet supported"
