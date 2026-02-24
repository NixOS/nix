#!/usr/bin/env bash

source common.sh

# Tests covered by lang tests:
# - Default: unquoted URLs accepted → eval-okay-url-literal-default
# - Fatal: unquoted URLs rejected → eval-fail-url-literal
# - Warn: produces warning → eval-okay-url-literal-warn
# - Quoted URLs accepted with fatal → eval-okay-url-literal-quoted-fatal

# Test: URLs with parameters (which must be quoted) are accepted
nix eval --lint-url-literals fatal --expr '"http://example.com?foo=bar"' 2>&1 | grepQuietInverse "error:"

# Test: The setting can be enabled via NIX_CONFIG
expect 1 env NIX_CONFIG='lint-url-literals = fatal' nix eval --expr 'http://example.com' 2>&1 | grepQuiet "error: URL literal"

# Test: Using old experimental feature name produces helpful warning
nix eval --extra-experimental-features no-url-literals --expr '"test"' 2>&1 \
    | grepQuiet "experimental feature 'no-url-literals' has been stabilized and renamed; use 'lint-url-literals = fatal' setting instead"
