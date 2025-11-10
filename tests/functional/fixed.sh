#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

path=$(nix-store -q "$(nix-instantiate fixed.nix -A good.0)")

echo 'testing bad...'
nix-build fixed.nix -A bad --no-out-link && fail "should fail"

# Building with the bad hash should produce the "good" output path as
# a side-effect.
[[ -e $path ]]
nix path-info --json "$path" | jq -e \
    --arg hash "$(nix hash convert --to base64 "md5:8ddd8be4b179a529afa5f2ffae4b9858")" \
    '.[].ca == {
        method: "flat",
        hash: {
            algorithm: "md5",
            format: "base64",
            hash: $hash
        },
    }'

echo 'testing good...'
nix-build fixed.nix -A good --no-out-link

if isDaemonNewer "2.4pre20210927"; then
    echo 'testing --check...'
    nix-build fixed.nix -A check --check && fail "should fail"
fi

echo 'testing good2...'
nix-build fixed.nix -A good2 --no-out-link

echo 'testing reallyBad...'
nix-instantiate fixed.nix -A reallyBad && fail "should fail"

if isDaemonNewer "2.20pre20240108"; then
    echo 'testing fixed with references...'
    expectStderr 1 nix-build fixed.nix -A badReferences | grepQuiet "not allowed to refer to other store paths"
fi

# While we're at it, check attribute selection a bit more.
echo 'testing attribute selection...'
test "$(nix-instantiate fixed.nix -A good.1 | wc -l)" = 1

# Test parallel builds of derivations that produce the same output.
# Only one should run at the same time.
echo 'testing parallelSame...'
clearStore
nix-build fixed.nix -A parallelSame --no-out-link -j2

# Fixed-output derivations with a recursive SHA-256 hash should
# produce the same path as "nix-store --add".
echo 'testing sameAsAdd...'
out=$(nix-build fixed.nix -A sameAsAdd --no-out-link)

# This is what fixed.builder2 produces...
rm -rf "$TEST_ROOT"/fixed
mkdir "$TEST_ROOT"/fixed
mkdir "$TEST_ROOT"/fixed/bla
echo "Hello World!" > "$TEST_ROOT"/fixed/foo
ln -s foo "$TEST_ROOT"/fixed/bar

out2=$(nix-store --add "$TEST_ROOT"/fixed)
[ "$out" = "$out2" ]

out3=$(nix-store --add-fixed --recursive sha256 "$TEST_ROOT"/fixed)
[ "$out" = "$out3" ]

out4=$(nix-store --print-fixed-path --recursive sha256 "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik" fixed)
[ "$out" = "$out4" ]

# Can use `outputHashMode = "nar";` instead of `"recursive"` now.
clearStore
nix-build fixed.nix -A nar-not-recursive --no-out-link
