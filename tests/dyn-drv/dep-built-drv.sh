#!/usr/bin/env bash

source common.sh

out1=$(nix-build ./text-hashed-output.nix -A hello --no-out-link)

clearStore

out2=$(nix-build ./text-hashed-output.nix -A wrapper --no-out-link)

diff -r $out1 $out2
