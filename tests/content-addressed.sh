#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

testDerivation () {
    derivationPath=$1
    out1=$(nix-build ./content-addressed.nix -A $derivationPath --arg seed 1 -vvvv)
    out2=$(nix-build ./content-addressed.nix -A $derivationPath --arg seed 2 -vvvv)
    test $out1 == $out2
}

testDerivation contentAddressed
testDerivation dependent
testDerivation transitivelyDependent
