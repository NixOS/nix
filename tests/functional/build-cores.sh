#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

echo "Testing build-cores configuration behavior..."

# Test 1: When build-cores is set to a non-zero value, NIX_BUILD_CORES should have that value
echo "Testing build-cores=4..."
rm -f "$TEST_ROOT"/build-cores-output
nix-build --cores 4 build-cores.nix -A testCores -o "$TEST_ROOT"/build-cores-output
result=$(cat "$(readlink "$TEST_ROOT"/build-cores-output)")
if [[ "$result" != "4" ]]; then
    echo "FAIL: Expected NIX_BUILD_CORES=4, got $result"
    exit 1
fi
echo "PASS: build-cores=4 correctly sets NIX_BUILD_CORES=4"
rm -f "$TEST_ROOT"/build-cores-output

# Test 2: When build-cores is set to 0, NIX_BUILD_CORES should be resolved to getDefaultCores()
echo "Testing build-cores=0..."
nix-build --cores 0 build-cores.nix -A testCores -o "$TEST_ROOT"/build-cores-output
result=$(cat "$(readlink "$TEST_ROOT"/build-cores-output)")
if [[ "$result" == "0" ]]; then
    echo "FAIL: NIX_BUILD_CORES should not be 0 when build-cores=0"
    exit 1
fi
echo "PASS: build-cores=0 resolves to NIX_BUILD_CORES=$result (should be > 0)"
rm -f "$TEST_ROOT"/build-cores-output

echo "All build-cores tests passed!"
