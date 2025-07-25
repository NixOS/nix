#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Test 1: By default, unquoted URLs are accepted
nix eval --expr 'http://example.com' >"$TEST_ROOT/stdout" 2>"$TEST_ROOT/stderr"
grepQuietInverse "error: URL literals are disabled" "$TEST_ROOT/stderr"

# Test 2: With the experimental feature enabled, unquoted URLs are rejected
nix eval --extra-experimental-features 'no-url-literals' --expr 'http://example.com' >"$TEST_ROOT/stdout" 2>"$TEST_ROOT/stderr" || true
grepQuiet "error: URL literals are disabled" "$TEST_ROOT/stderr"

# Test 3: Quoted URLs are always accepted
nix eval --extra-experimental-features 'no-url-literals' --expr '"http://example.com"' >"$TEST_ROOT/stdout" 2>"$TEST_ROOT/stderr"
grepQuietInverse "error: URL literals are disabled" "$TEST_ROOT/stderr"

# Test 4: URLs with parameters (which must be quoted) are accepted
nix eval --extra-experimental-features 'no-url-literals' --expr '"http://example.com?foo=bar"' >"$TEST_ROOT/stdout" 2>"$TEST_ROOT/stderr"
grepQuietInverse "error: URL literals are disabled" "$TEST_ROOT/stderr"

# Test 5: The feature can be enabled via NIX_CONFIG
NIX_CONFIG='extra-experimental-features = no-url-literals' nix eval --expr 'http://example.com' >"$TEST_ROOT/stdout" 2>"$TEST_ROOT/stderr" || true
grepQuiet "error: URL literals are disabled" "$TEST_ROOT/stderr"

# Test 6: The feature can be enabled via CLI even if not set in config
NIX_CONFIG='' nix eval --extra-experimental-features 'no-url-literals' --expr 'http://example.com' >"$TEST_ROOT/stdout" 2>"$TEST_ROOT/stderr" || true
grepQuiet "error: URL literals are disabled" "$TEST_ROOT/stderr"

# Test 7: Evaluation still works for quoted URLs
result=$(nix eval --raw --extra-experimental-features no-url-literals --expr '"http://example.com"' 2>/dev/null)
expected="http://example.com"
[[ "$result" == "$expected" ]] || fail "Evaluation result should be correct for quoted URLs"

echo "no-url-literals test passed!"
