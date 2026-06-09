#!/usr/bin/env bash

source common.sh

# Tests for absolute path literals that require NIX_CONFIG or grepQuietInverse.
# Basic warn/fatal/default behavior tests are in lang/eval-*-abs-path-*.nix

clearStoreIfPossible

# Test: Setting via NIX_CONFIG
NIX_CONFIG='lint-absolute-path-literals = warn' nix eval --expr '/tmp/bar' 2>"$TEST_ROOT"/stderr
grepQuiet "absolute path literals are not portable" "$TEST_ROOT/stderr"

# Test: Command line overrides config
NIX_CONFIG='lint-absolute-path-literals = warn' nix eval --lint-absolute-path-literals ignore --expr '/tmp/bar' 2>"$TEST_ROOT"/stderr
grepQuietInverse "absolute path literal" "$TEST_ROOT/stderr"

echo "absolute-path-literals test passed!"
