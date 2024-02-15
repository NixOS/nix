#!/usr/bin/env bash

source ./common.sh

# Without the dynamic-derivations XP feature, we don't have the builtin.
nix --experimental-features 'nix-command' eval --impure  --expr \
    'assert ! (builtins ? outputOf); ""'

# Test that a string is required.
#
# We currently require a string to be passed, rather than a derivation
# object that could be coerced to a string. We might liberalise this in
# the future so it does work, but there are some design questions to
# resolve first. Adding a test so we don't liberalise it by accident.
expectStderr 1 nix --experimental-features 'nix-command dynamic-derivations' eval --impure --expr \
    'builtins.outputOf (import ../dependencies.nix {}) "out"' \
    | grepQuiet "expected a string but found a set"

# Test that "DrvDeep" string contexts are not supported at this time
#
# Like the above, this is a restriction we could relax later.
expectStderr 1 nix --experimental-features 'nix-command dynamic-derivations' eval --impure --expr \
    'builtins.outputOf (import ../dependencies.nix {}).drvPath "out"' \
    | grepQuiet "has a context which refers to a complete source and binary closure. This is not supported at this time"

# Test using `builtins.outputOf` with static derivations
testStaticHello () {
    nix eval --impure --expr \
        'with (import ./text-hashed-output.nix); let
           a = hello.outPath;
           b = builtins.outputOf (builtins.unsafeDiscardOutputDependency hello.drvPath) "out";
         in builtins.trace a
           (builtins.trace b
             (assert a == b; null))'
}

# Test with a regular old input-addresed derivation
#
# `builtins.outputOf` works without ca-derivations and doesn't create a
# placeholder but just returns the output path.
testStaticHello

# Test with content addressed derivation.
NIX_TESTS_CA_BY_DEFAULT=1 testStaticHello

# Test with derivation-producing derivation
#
# This is hardly different from the preceding cases, except that we're
# only taking 1 outputOf out of 2 possible outputOfs. Note that
# `.outPath` could be defined as `outputOf drvPath`, which is what we're
# testing here. The other `outputOf` that we're not testing here is the
# use of _dynamic_ derivations.
nix eval --impure --expr \
    'with (import ./text-hashed-output.nix); let
       a = producingDrv.outPath;
       b = builtins.outputOf (builtins.builtins.unsafeDiscardOutputDependency producingDrv.drvPath) "out";
     in builtins.trace a
       (builtins.trace b
         (assert a == b; null))'

# Test with unbuilt output of derivation-producing derivation.
#
# This function similar to `testStaticHello` used above, but instead of
# checking the property on a constant derivation, we check it on a
# derivation that's from another derivation's output (outPath).
testDynamicHello () {
    nix eval --impure --expr \
        'with (import ./text-hashed-output.nix); let
           a = builtins.outputOf producingDrv.outPath "out";
           b = builtins.outputOf (builtins.outputOf (builtins.unsafeDiscardOutputDependency producingDrv.drvPath) "out") "out";
         in builtins.trace a
           (builtins.trace b
             (assert a == b; null))'
}

# inner dynamic derivation is input-addressed
testDynamicHello

# inner dynamic derivation is content-addressed
NIX_TESTS_CA_BY_DEFAULT=1 testDynamicHello
