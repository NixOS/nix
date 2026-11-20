#!/usr/bin/env bash

source common.sh

# Build an output of a derivation that is two dynamic layers deep: the
# output of `producingProducingDrv` is the derivation `producingDrv`, whose
# output is the derivation `hello`. Do it once with the store the test
# starts with, then through a daemon started here, which receives the
# request over the worker protocol.

out1=$(nix build -f ./text-hashed-output.nix hello --no-link --print-out-paths)

clearStore

drvDep=$(nix-instantiate ./text-hashed-output.nix -A producingProducingDrv)
out2=$(nix build "${drvDep}^out^out^out" --no-link --print-out-paths)
test "$out1" == "$out2"

clearStore

drvDep=$(nix-instantiate ./text-hashed-output.nix -A producingProducingDrv)
startDaemon
out3=$(nix build "${drvDep}^out^out^out" --no-link --print-out-paths)
test "$out1" == "$out3"
