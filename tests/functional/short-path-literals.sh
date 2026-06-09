#!/usr/bin/env bash

source common.sh

# Tests covered by lang tests:
# - Warning for short paths → eval-okay-short-path-literal-warn
# - Fatal for short paths → eval-fail-short-path-literal
# - Variation paths (foo/bar, a/b/c/d) → eval-okay-short-path-variation
# - ./ paths with fatal → eval-okay-dotslash-path-fatal
# - ../ paths with fatal → eval-okay-dotdotslash-path-fatal

# Tests for the deprecated --warn-short-path-literals boolean setting

# Test: Without the setting (default), no warnings should be produced
nix eval --expr 'test/subdir' 2>"$TEST_ROOT"/stderr
grepQuietInverse < "$TEST_ROOT/stderr" -E "relative path|path literal" || fail "Should not produce warnings by default"

# Test: Paths starting with ./ should NOT produce warnings
nix eval --warn-short-path-literals --expr './test/subdir' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test: Paths starting with ../ should NOT produce warnings
nix eval --warn-short-path-literals --expr '../test/subdir' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test: Absolute paths should NOT produce warnings
nix eval --warn-short-path-literals --expr '/absolute/path' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test: Test with nix-instantiate as well
nix-instantiate --warn-short-path-literals --eval -E 'foo/bar' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'foo/bar' should be prefixed" "$TEST_ROOT/stderr"

# Test: Test that the deprecated setting can be set via configuration
NIX_CONFIG='warn-short-path-literals = true' nix eval --expr 'test/file' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'test/file' should be prefixed" "$TEST_ROOT/stderr"

# Test: Test that command line flag overrides config
NIX_CONFIG='warn-short-path-literals = true' nix eval --no-warn-short-path-literals --expr 'test/file' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Tests for NIX_CONFIG and setting precedence

# Test: New setting via NIX_CONFIG
NIX_CONFIG='lint-short-path-literals = warn' nix eval --expr 'test/file' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'test/file' should be prefixed" "$TEST_ROOT/stderr"

# Test: New setting overrides deprecated setting
NIX_CONFIG='warn-short-path-literals = true' nix eval --lint-short-path-literals ignore --expr 'test/file' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test: Explicit new setting takes precedence (error over deprecated warn)
NIX_CONFIG='warn-short-path-literals = true' expectStderr 1 nix eval --lint-short-path-literals fatal --expr 'test/subdir' \
    | grepQuiet "error:"
