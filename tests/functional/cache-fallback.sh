#!/bin/bash

# Test script for Nix cache fallback improvements
# This script tests various cache failure scenarios to verify robust fallback behavior

set -e

echo "=== Nix Cache Fallback Test Suite ==="
echo "Testing cache preservation and fallback behavior..."
echo

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counter
TESTS_PASSED=0
TESTS_TOTAL=0

run_test() {
    local test_name="$1"
    local test_cmd="$2"
    local expected_pattern="$3"
    
    echo -e "${YELLOW}Test $((++TESTS_TOTAL)): $test_name${NC}"
    echo "Command: $test_cmd"
    
    if eval "$test_cmd" | grep -q "$expected_pattern"; then
        echo -e "${GREEN}✓ PASSED${NC}"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗ FAILED${NC}"
        echo "Expected pattern: $expected_pattern"
        echo "Actual output:"
        eval "$test_cmd" | head -10
    fi
    echo "---"
}

# Test 1: Single bad cache fallback to good cache
run_test \
    "Single bad cache fallback" \
    "nix build --option substituters 'https://nonexistent.invalid https://cache.nixos.org' nixpkgs#hello --dry-run --verbose 2>&1" \
    "querying info about.*on 'https://cache.nixos.org'"

# Test 2: Multiple bad caches with one good cache
run_test \
    "Multiple bad caches fallback" \
    "nix build --option substituters 'https://bad1.invalid https://bad2.invalid https://cache.nixos.org' nixpkgs#hello --dry-run --verbose 2>&1" \
    "downloading 'https://bad1.invalid/nix-cache-info'"

# Test 3: All caches working (parallel querying)
run_test \
    "Parallel cache querying" \
    "nix build --option substituters 'https://euler.cachix.org https://cache.nixos.org' nixpkgs#firefox --dry-run --verbose 2>&1" \
    "querying info about.*on 'https://euler.cachix.org'"

# Test 4: DNS resolution failure handling
run_test \
    "DNS resolution failure handling" \
    "nix build --option substituters 'https://nonexistent.cache.invalid https://cache.nixos.org' nixpkgs#hello --dry-run --verbose 2>&1" \
    "Couldn't resolve host name"

# Test 5: Mixed failure modes
run_test \
    "Mixed failure modes handling" \
    "nix build --option substituters 'https://timeout.invalid https://dns-fail.invalid https://cache.nixos.org' nixpkgs#hello --dry-run --verbose 2>&1" \
    "downloading.*cache-info"

# Test 6: Cache priority ordering preservation
run_test \
    "Cache priority ordering" \
    "nix build --option substituters 'https://cache.nixos.org https://euler.cachix.org' nixpkgs#hello --dry-run --verbose 2>&1" \
    "cache.nixos.org"

# Test 7: Large package with multiple dependencies
run_test \
    "Large package multi-dependency fallback" \
    "nix build --option substituters 'https://bad.invalid https://cache.nixos.org https://euler.cachix.org' nixpkgs#firefox --dry-run --verbose 2>&1" \
    "downloading.*narinfo"

# Test 8: No caches specified (should use defaults)
run_test \
    "Default cache behavior" \
    "nix build nixpkgs#hello --dry-run --verbose 2>&1" \
    "querying info about"

echo
echo "=== Test Results ==="
echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}/$TESTS_TOTAL"

if [ $TESTS_PASSED -eq $TESTS_TOTAL ]; then
    echo -e "${GREEN}All tests passed! Cache fallback is working correctly.${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. Review the output above.${NC}"
    exit 1
fi
