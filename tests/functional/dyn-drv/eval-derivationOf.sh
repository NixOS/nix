#!/usr/bin/env bash

source ./common.sh

# Unlike outputOf, derivationOf does not require dynamic-derivations.
nix --experimental-features 'nix-command' eval --impure --expr \
    'assert builtins ? derivationOf; ""'

# Test that derivationOf unwraps the dynamic derivation reference in an
# unbuilt output path. In particular, this must not require the producing
# derivation to have been built or imported into the store.
testDynamicDerivation () {
    nix eval --impure --expr \
        'with (import ./text-hashed-output.nix); let
           expected = producingDrv.outPath;
           output = builtins.outputOf expected "out";
           check = value: let actual = builtins.derivationOf value;
             in assert actual == expected;
               assert builtins.getContext actual == builtins.getContext expected;
               true;
         in assert check output;
           check { outPath = output; }'
}

# Inner dynamic derivation is input-addressed.
testDynamicDerivation

# Inner dynamic derivation is content-addressed.
NIX_TESTS_CA_BY_DEFAULT=1 testDynamicDerivation
