#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStoreIfPossible

rm -f "$TEST_ROOT"/result*

# Placeholder strings are opaque, so cannot do this check for floating
# content-addressing derivations.
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # Test whether the output names match our expectations
    outPath=$(nix-instantiate multiple-outputs.nix --eval -A nameCheck.out.outPath)
    # shellcheck disable=SC2016
    [ "$(echo "$outPath" | sed -E 's_^".*/[^-/]*-([^/]*)"$_\1_')" = "multiple-outputs-a" ]
    outPath=$(nix-instantiate multiple-outputs.nix --eval -A nameCheck.dev.outPath)
    # shellcheck disable=SC2016
    [ "$(echo "$outPath" | sed -E 's_^".*/[^-/]*-([^/]*)"$_\1_')" = "multiple-outputs-a-dev" ]
fi

# Test whether read-only evaluation works when referring to the
# ‘drvPath’ attribute.
echo "evaluating c..."
#drvPath=$(nix-instantiate multiple-outputs.nix -A c --readonly-mode)

# And check whether the resulting derivation explicitly depends on all
# outputs.
drvPath=$(nix-instantiate multiple-outputs.nix -A c)
#[ "$drvPath" = "$drvPath2" ]
grepQuiet 'multiple-outputs-a.drv",\["first","second"\]' "$drvPath"
grepQuiet 'multiple-outputs-b.drv",\["out"\]' "$drvPath"

# While we're at it, test the ‘unsafeDiscardOutputDependency’ primop.
outPath=$(nix-build multiple-outputs.nix -A d --no-out-link)
drvPath=$(cat "$outPath"/drv)
if [[ -n "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    expectStderr 1 nix-store -q "$drvPath" | grepQuiet "Cannot use output path of floating content-addressing derivation until we know what it is (e.g. by building it)"
else
    outPath=$(nix-store -q "$drvPath")
    # shellcheck disable=SC2233
    (! [ -e "$outPath" ])
fi

# Do a build of something that depends on a derivation with multiple
# outputs.
echo "building b..."
outPath=$(nix-build multiple-outputs.nix -A b --no-out-link)
echo "output path is $outPath"
[ "$(cat "$outPath/file")" = "success" ]

# Test nix-build on a derivation with multiple outputs.
outPath1=$(nix-build multiple-outputs.nix -A a -o "$TEST_ROOT"/result)
[ -e "$TEST_ROOT"/result-first ]
# shellcheck disable=SC2235
(! [ -e "$TEST_ROOT"/result-second ])
nix-build multiple-outputs.nix -A a.all -o "$TEST_ROOT"/result
[ "$(cat "$TEST_ROOT"/result-first/file)" = "first" ]
[ "$(cat "$TEST_ROOT"/result-second/file)" = "second" ]
[ "$(cat "$TEST_ROOT"/result-second/link/file)" = "first" ]
hash1=$(nix-store -q --hash "$TEST_ROOT"/result-second)

outPath2=$(nix-build "$(nix-instantiate multiple-outputs.nix -A a)" --no-out-link)
[[ $outPath1 = "$outPath2" ]]

outPath2=$(nix-build "$(nix-instantiate multiple-outputs.nix -A a.first)" --no-out-link)
[[ $outPath1 = "$outPath2" ]]

outPath2=$(nix-build "$(nix-instantiate multiple-outputs.nix -A a.second)" --no-out-link)
[[ $(cat "$outPath2"/file) = second ]]

# FIXME: Fixing this shellcheck causes the test to fail.
# shellcheck disable=SC2046
[[ $(nix-build $(nix-instantiate multiple-outputs.nix -A a.all) --no-out-link | wc -l) -eq 2 ]]

if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # Delete one of the outputs and rebuild it.  This will cause a hash
    # rewrite.
    env -u NIX_REMOTE nix store delete "$TEST_ROOT"/result-second --ignore-liveness
    nix-build multiple-outputs.nix -A a.all -o "$TEST_ROOT"/result
    [ "$(cat "$TEST_ROOT"/result-second/file)" = "second" ]
    [ "$(cat "$TEST_ROOT"/result-second/link/file)" = "first" ]
    hash2=$(nix-store -q --hash "$TEST_ROOT"/result-second)
    [ "$hash1" = "$hash2" ]
fi

# Make sure that nix-build works on derivations with multiple outputs.
echo "building a.first..."
nix-build multiple-outputs.nix -A a.first --no-out-link

# Cyclic outputs should be rejected.
echo "building cyclic..."
if nix-build multiple-outputs.nix -A cyclic --no-out-link; then
    echo "Cyclic outputs incorrectly accepted!"
    exit 1
fi

# Do a GC. This should leave an empty store.
echo "collecting garbage..."
rm "$TEST_ROOT"/result*
nix-store --gc --keep-derivations --keep-outputs
nix-store --gc --print-roots
rm -rf "$NIX_STORE_DIR"/.links
rmdir "$NIX_STORE_DIR"

# TODO inspect why this doesn't work with floating content-addressing
# derivations.
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    expect 1 nix build -f multiple-outputs.nix invalid-output-name-1 2>&1 | grep 'contains illegal character'
    expect 1 nix build -f multiple-outputs.nix invalid-output-name-2 2>&1 | grep 'contains illegal character'
fi
