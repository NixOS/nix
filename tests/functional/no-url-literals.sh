#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Test 1: By default, unquoted URLs are accepted
nix eval --expr 'http://example.com' 2>&1 | grepQuietInverse "error: URL literals are disabled"

# Test 2: With the experimental feature enabled, unquoted URLs are rejected
expect 1 nix eval --extra-experimental-features 'no-url-literals' --expr 'http://example.com' 2>&1 | grepQuiet "error: URL literals are disabled"

# Test 3: Quoted URLs are always accepted
nix eval --extra-experimental-features 'no-url-literals' --expr '"http://example.com"' 2>&1 | grepQuietInverse "error: URL literals are disabled"

# Test 4: URLs with parameters (which must be quoted) are accepted
nix eval --extra-experimental-features 'no-url-literals' --expr '"http://example.com?foo=bar"' 2>&1 | grepQuietInverse "error: URL literals are disabled"

# Test 5: The feature can be enabled via NIX_CONFIG
expect 1 env NIX_CONFIG='extra-experimental-features = no-url-literals' nix eval --expr 'http://example.com' 2>&1 | grepQuiet "error: URL literals are disabled"

# Test 6: The feature can be enabled via CLI even if not set in config
expect 1 env NIX_CONFIG='' nix eval --extra-experimental-features 'no-url-literals' --expr 'http://example.com' 2>&1 | grepQuiet "error: URL literals are disabled"

# Test 7: Evaluation still works for quoted URLs
nix eval --raw --extra-experimental-features no-url-literals --expr '"http://example.com"' | grepQuiet "^http://example.com$"

echo "no-url-literals test passed!"
