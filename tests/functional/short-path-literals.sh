#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Test 1: Without the setting (default), no warnings should be produced
nix eval --expr 'test/subdir' 2>"$TEST_ROOT"/stderr
grepQuietInverse < "$TEST_ROOT/stderr" -E "relative path|path literal" || fail "Should not produce warnings by default"

# Test 2: With the setting enabled, warnings should be produced for short path literals
nix eval --warn-short-path-literals --expr 'test/subdir' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'test/subdir' should be prefixed with '.' for clarity: './test/subdir'" "$TEST_ROOT/stderr"

# Test 3: Different short path literals should all produce warnings
nix eval --warn-short-path-literals --expr 'foo/bar' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'foo/bar' should be prefixed with '.' for clarity: './foo/bar'" "$TEST_ROOT/stderr"

nix eval --warn-short-path-literals --expr 'a/b/c/d' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'a/b/c/d' should be prefixed with '.' for clarity: './a/b/c/d'" "$TEST_ROOT/stderr"

# Test 4: Paths starting with ./ should NOT produce warnings
nix eval --warn-short-path-literals --expr './test/subdir' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test 5: Paths starting with ../ should NOT produce warnings
nix eval --warn-short-path-literals --expr '../test/subdir' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test 6: Absolute paths should NOT produce warnings
nix eval --warn-short-path-literals --expr '/absolute/path' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

# Test 7: Test that the warning is at the correct position
nix eval --warn-short-path-literals --expr 'foo/bar' 2>"$TEST_ROOT"/stderr
grepQuiet "at «string»:1:1:" "$TEST_ROOT/stderr"

# Test 8: Test that evaluation still works correctly despite the warning
result=$(nix eval --warn-short-path-literals --expr 'test/subdir' 2>/dev/null)
expected="$PWD/test/subdir"
[[ "$result" == "$expected" ]] || fail "Evaluation result should be correct despite warning"

# Test 9: Test with nix-instantiate as well
nix-instantiate --warn-short-path-literals --eval -E 'foo/bar' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'foo/bar' should be prefixed" "$TEST_ROOT/stderr"

# Test 10: Test that the setting can be set via configuration
NIX_CONFIG='warn-short-path-literals = true' nix eval --expr 'test/file' 2>"$TEST_ROOT"/stderr
grepQuiet "relative path literal 'test/file' should be prefixed" "$TEST_ROOT/stderr"

# Test 11: Test that command line flag overrides config
NIX_CONFIG='warn-short-path-literals = true' nix eval --no-warn-short-path-literals --expr 'test/file' 2>"$TEST_ROOT"/stderr
grepQuietInverse "relative path literal" "$TEST_ROOT/stderr"

echo "short-path-literals test passed!"
