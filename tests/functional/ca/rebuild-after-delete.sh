#!/usr/bin/env bash

# A non-reproducible CA derivation whose output was deleted builds again (NixOS/nix#15649).

source common.sh

requireDaemonNewerThan "2.36.0pre20260916"

clearStore

drv=$(nix-instantiate ./nondeterministic-ns.nix -A singleOut)

first=$(nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link)
nix-store --delete "$first"

# The build trace of the deleted path gives way to the new one.
second=$(nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link)
[[ -e $second ]]
[[ $(nix store build-trace info "$drv"^out --json | jq -r '.[] | .opaquePath | select(.)') == "$second" ]]

