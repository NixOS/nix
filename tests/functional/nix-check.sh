#!/usr/bin/env bash

source common.sh

TODO_NixOS

###################################################
# Test that nix check requires explicit installables

output=$(nix check 2>&1) && fail "nix check with no arguments should fail"
echo "$output" | grepQuiet "requires at least one installable"
echo "$output" | grepQuiet "Did you mean 'nix flake check'?"

###################################################
# Test that nix check rejects bare flake references

# Create a minimal flake for testing
emptyFlake=$TEST_ROOT/empty-flake
mkdir -p "$emptyFlake"
cat > "$emptyFlake/flake.nix" << 'EOF'
{
  outputs = { self }: { };
}
EOF
git -C "$emptyFlake" init
git -C "$emptyFlake" add flake.nix

output=$(nix check "$emptyFlake" 2>&1) && fail "nix check with bare flake reference should fail"
echo "$output" | grepQuiet "does not specify which outputs to check"
echo "$output" | grepQuiet "#<output>"
echo "$output" | grepQuiet "nix flake check"

###################################################
# Test basic functionality: check derivations and report paths

clearStore
clearCache

input1_drvPath=$(nix eval -f dependencies.nix input1_drv.drvPath --raw)
input2_drvPath=$(nix eval -f dependencies.nix input2_drv.drvPath --raw)

# Single derivation
output=$(nix check -f dependencies.nix input1_drv 2>&1)
echo "$output" | grepQuiet "$input1_drvPath: OK (available)"

# Multiple derivations
output=$(nix check -f dependencies.nix input1_drv input2_drv 2>&1)
echo "$output" | grepQuiet "$input1_drvPath: OK (available)"
echo "$output" | grepQuiet "$input2_drvPath: OK (available)"

###################################################
# Test dry-run doesn't build

clearStore
clearCache

# With no substituters, this needs to be built
output=$(nix check -f dependencies.nix input1_drv --dry-run 2>&1)
echo "$output" | grepQuiet "would be built"
# Verify nothing was actually built
! nix path-info -f dependencies.nix input1_drv 2>/dev/null || fail "dry-run built the derivation"

###################################################
# Test no result symlinks created

RESULT=$TEST_ROOT/result
rm -f "$RESULT"*
nix check -f dependencies.nix input1_drv input2_drv
[[ ! -h $RESULT ]] || fail "nix check created result symlink"

###################################################
# Test opaque store paths

clearStore
clearCache

storePath=$(nix build -f dependencies.nix input1_drv --no-link --print-out-paths)
output=$(nix check "$storePath" 2>&1)
echo "$output" | grepQuiet "OK (opaque path)"

###################################################
# Test failing derivation

clearStore
clearCache

# Create a derivation that will fail
cat > "$TEST_ROOT/failing.nix" <<EOF
with import ./config.nix;
mkDerivation {
  name = "failing-drv";
  buildCommand = "exit 1";
}
EOF

# Check should fail when derivation fails to build
! nix check -f "$TEST_ROOT/failing.nix" 2>&1 || fail "nix check succeeded on failing derivation"

###################################################
# Test optimization:
#     don't download substitutable paths

if [[ -n "${NIX_REMOTE:-}" ]]; then
    echo "Skipping substituter test with daemon"
else
    clearStore
    clearCacheCache

    # Set up binary cache with a built derivation
    outPath=$(nix-build dependencies.nix --no-out-link -A input1_drv)
    input1_drvPath=$(nix eval -f dependencies.nix input1_drv.drvPath --raw)
    nix copy --to "file://$cacheDir" "$outPath"

    # Keep the .drv in the store so queryMissing can determine output paths
    # but delete the actual output
    nix-store --delete "$outPath"

    # Dry-run should report it can be fetched
    clearCacheCache

    output=$(nix check -f dependencies.nix input1_drv --dry-run --substituters "file://$cacheDir" --no-require-sigs --option substitute true 2>&1)
    echo "$output" | grepQuiet "can be fetched"

    # Check should succeed without downloading
    output=$(nix check -f dependencies.nix input1_drv --substituters "file://$cacheDir" --no-require-sigs --option substitute true 2>&1)
    echo "$output" | grepQuiet "$input1_drvPath: OK (available)"

    # Verify it didn't download
    echo "$output" | grepQuietInverse "copying path.*$outPath" || fail "downloaded substitutable path"
    ! nix path-info "$outPath" 2>/dev/null || fail "path exists in local store after check"
fi
