#!/usr/bin/env bash

source common.sh

TODO_NixOS

###################################################
# Check that --dry-run isn't confused with read-only mode
# https://github.com/NixOS/nix/issues/1795

# Ensure this builds successfully first
nix build --no-link -f dependencies.nix

clearStore

# Try --dry-run using old command first
nix-build --no-out-link dependencies.nix --dry-run 2>&1 | grep "will be built"
# Now new command:
nix build -f dependencies.nix --dry-run 2>&1 | grep "will be built"

clearStore

# Try --dry-run using new command first
nix build -f dependencies.nix --dry-run 2>&1 | grep "will be built"
# Now old command:
nix-build --no-out-link dependencies.nix --dry-run 2>&1 | grep "will be built"

###################################################
# Check --dry-run doesn't create links with --dry-run
# https://github.com/NixOS/nix/issues/1849
clearStore

RESULT=$TEST_ROOT/result-link
rm -f "$RESULT"

nix-build dependencies.nix -o "$RESULT" --dry-run

[[ ! -h $RESULT ]] || fail "nix-build --dry-run created output link"

nix build -f dependencies.nix -o "$RESULT" --dry-run

[[ ! -h $RESULT ]] || fail "nix build --dry-run created output link"

nix build -f dependencies.nix -o "$RESULT"

[[ -h $RESULT ]]

###################################################
# Check the JSON output
clearStore

RES=$(nix build -f dependencies.nix --dry-run --json)

if [[ -z "${NIX_TESTS_CA_BY_DEFAULT-}" ]]; then
    echo "$RES" | jq '.[0] | [
        (.drvPath | test("'"$NIX_STORE_DIR"'.*\\.drv")),
        (.outputs.out | test("'"$NIX_STORE_DIR"'"))
    ] | all'
else
    echo "$RES" | jq '.[0] | [
        (.drvPath | test("'"$NIX_STORE_DIR"'.*\\.drv")),
        .outputs.out == null
    ] | all'
fi
