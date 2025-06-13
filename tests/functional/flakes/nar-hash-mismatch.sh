#!/usr/bin/env bash

source ./common.sh

requireGit

clearStore
clearCache

# Create a test flake with NAR hash mismatch
tmpDir=$TEST_ROOT/nar-hash-test
rm -rf "$tmpDir"
mkdir -p "$tmpDir"
cd "$tmpDir"

# Setup git repo with a sub-flake
initGitRepo .
mkdir sub
echo '{ outputs = { self }: { test = "hello"; }; }' > sub/flake.nix
git add sub
git commit -m "add sub"

# Get the original hash and create main flake that references it
hash=$(nix hash path ./sub)
echo "$hash" > sub.narHash

cat > flake.nix << EOF
{
  outputs = { self }:
    let
      hash = builtins.readFile ./sub.narHash;
      cleanHash = builtins.substring 0 (builtins.stringLength hash - 1) hash;
      subFlake = builtins.getFlake "path:\${toString ./sub}?narHash=\${cleanHash}";
    in
    { inherit (subFlake) test; };
}
EOF

git add flake.nix sub.narHash

# Modify sub-flake to create hash mismatch
echo '{ outputs = { self }: { test = "modified"; }; }' > sub/flake.nix

# Test that evaluation fails with proper error message (not assertion failure)
if output=$(nix eval .#test 2>&1); then
    fail "Expected evaluation to fail, but it succeeded"
fi

# Verify error message contains expected content and no crash indicators
grepQuiet "NAR hash mismatch" <<< "$output" || fail "Expected 'NAR hash mismatch' in error output"
grepQuietInverse "Assertion.*failed" <<< "$output" || fail "Should not contain assertion failure"
grepQuietInverse "Aborted" <<< "$output" || fail "Should not contain 'Aborted'"