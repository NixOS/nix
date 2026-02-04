#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Unquoted URLs are rejected
expect 1 nix eval --expr 'http://example.com' 2>&1 | grepQuiet "error: URL literals are disabled"

# But accepted when explicitly enabled as a deprecated feature
nix eval --extra-deprecated-features url-literals --expr 'http://example.com' 2>&1 | grepQuietInverse "error: URL literals are disabled"

# Quoted URLs are always accepted
nix eval --expr '"http://example.com"' 2>&1 | grepQuietInverse "error: URL literals are disabled"

# URLs with parameters (which must be quoted) are accepted
nix eval --expr '"http://example.com?foo=bar"' 2>&1 | grepQuietInverse "error: URL literals are disabled"

# Evaluation still works for quoted URLs
nix eval --raw --expr '"http://example.com"' | grepQuiet "^http://example.com$"

echo "no-url-literals test passed!"
