#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

drvPath=$(nix-instantiate dependencies.nix)

echo "derivation is $drvPath"

nix-store -q --tree "$drvPath" | grep '───.*builder-dependencies-input-1.sh'

# Test Graphviz graph generation.
nix-store -q --graph "$drvPath" > "$TEST_ROOT"/graph
if test -n "$dot"; then
    # Does it parse?
    $dot < "$TEST_ROOT"/graph
fi

# Test GraphML graph generation
nix-store -q --graphml "$drvPath" > "$TEST_ROOT"/graphml

outPath=$(nix-store -rvv "$drvPath") || fail "build failed"

# Test Graphviz graph generation.
nix-store -q --graph "$outPath" > "$TEST_ROOT"/graph
if test -n "$dot"; then
    # Does it parse?
    $dot < "$TEST_ROOT"/graph
fi

nix-store -q --tree "$outPath" | grep '───.*dependencies-input-2'

echo "output path is $outPath"

text=$(cat "$outPath/foobar")
if test "$text" != "FOOBAR"; then exit 1; fi

deps=$(nix-store -quR "$drvPath")

echo "output closure contains $deps"

# The output path should be in the closure.
echo "$deps" | grepQuiet "$outPath"

# Input-1 is not retained.
if echo "$deps" | grepQuiet "dependencies-input-1"; then exit 1; fi

# Input-2 is retained.
input2OutPath=$(echo "$deps" | grep "dependencies-input-2")

# The referrers closure of input-2 should include outPath.
nix-store -q --referrers-closure "$input2OutPath" | grep "$outPath"

# Check that the derivers are set properly.
test "$(nix-store -q --deriver "$outPath")" = "$drvPath"
nix-store -q --deriver "$input2OutPath" | grepQuiet -- "-input-2.drv"

# --valid-derivers returns the currently single valid .drv file
test "$(nix-store -q --valid-derivers "$outPath")" = "$drvPath"

# instantiate a different drv with the same output
drvPath2=$(nix-instantiate dependencies.nix --argstr hashInvalidator yay)

# now --valid-derivers returns both
test "$(nix-store -q --valid-derivers "$outPath" | sort)" = "$(sort <<< "$drvPath"$'\n'"$drvPath2")"

TODO_NixOS # The following --delete fails, because it seems to be still alive. This might be caused by a different test using the same path. We should try make the derivations unique, e.g. naming after tests, and adding a timestamp that's constant for that test script run.

# check that nix-store --valid-derivers only returns existing drv
nix-store --delete "$drvPath"
test "$(nix-store -q --valid-derivers "$outPath")" = "$drvPath2"

# check that --valid-derivers returns nothing when there are no valid derivers
nix-store --delete "$drvPath2"
test -z "$(nix-store -q --valid-derivers "$outPath")"
