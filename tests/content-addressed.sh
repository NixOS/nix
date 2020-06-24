#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

out1=$(nix-build ./content-addressed.nix --arg seed 1)
out2=$(nix-build ./content-addressed.nix --arg seed 2)

test $out1 == $out2
