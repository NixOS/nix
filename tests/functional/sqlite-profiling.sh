#!/usr/bin/env bash

source common.sh

clearStoreIfPossible
clearCache

# Test that profiling is disabled by default
nix build -f simple.nix --no-link
[[ ! -f "nix-sqlite-profile.jsonl" ]] || fail "Profile created when not requested"

# Test basic profiling with default filename
export NIX_SQLITE_PROFILE=1
nix eval --expr '1 + 1' > /dev/null
[[ -f "nix-sqlite-profile.jsonl" ]] || fail "Default profile not created"
grep -q '"type":"start"' nix-sqlite-profile.jsonl || fail "Missing start event"
unset NIX_SQLITE_PROFILE
rm -f nix-sqlite-profile.jsonl

# Test profiling with custom path
profilePath="$TEST_ROOT/profile.jsonl"
export NIX_SQLITE_PROFILE="$profilePath"

# Run a build that will use multiple SQLite databases
nix build -f simple.nix --no-link

# Verify profile was created
[[ -f "$profilePath" ]] || fail "Profile not created at custom path"

# Check for expected events
grep -q '"type":"start"' "$profilePath" || fail "Missing start event"
grep -q '"database":".*/db.sqlite"' "$profilePath" || fail "Missing store database queries"
grep -q '"query":"BEGIN IMMEDIATE"' "$profilePath" || fail "Missing transaction queries"
grep -q '"query":"INSERT' "$profilePath" || fail "Missing insert queries"
grep -q '"query":"SELECT' "$profilePath" || fail "Missing select queries"

# Check that queries have timing information
if ! grep -q '"execution_time_ms":[0-9.]\+' "$profilePath"; then
    fail "Missing execution time in profile"
fi

# Test with eval cache
rm -f "$profilePath"
export NIX_SQLITE_PROFILE="$profilePath"
nix eval --expr 'builtins.derivation { name = "test"; builder = "/bin/sh"; system = "x86_64-linux"; }' > /dev/null

# Check for eval cache queries
grep -q '"database":".*/eval-cache-v' "$profilePath" || fail "Missing eval cache queries"

# Test with fetcher cache  
rm -f "$profilePath"
export NIX_SQLITE_PROFILE="$profilePath"
nix eval --impure --expr 'builtins.fetchTarball "https://github.com/NixOS/nix/archive/master.tar.gz"' 2>/dev/null || true

# Check for fetcher cache queries (if network is available)
if grep -q '"database":".*/fetcher-cache-v' "$profilePath"; then
    echo "Found fetcher cache queries (good)"
else
    echo "No fetcher cache queries (network might be unavailable)"
fi

# Test that profiling captures parameter values
rm -f "$profilePath"
export NIX_SQLITE_PROFILE="$profilePath"
nix-store --add-fixed sha256 simple.nix > /dev/null

# Look for parameterized queries with actual values
if grep -q '"query":"INSERT INTO ValidPaths.*VALUES.*(' "$profilePath"; then
    echo "Found expanded parameter values"
else
    fail "Parameter values not captured in queries"
fi

# Test JSON validity of entire profile
rm -f "$profilePath"
export NIX_SQLITE_PROFILE="$profilePath"
nix build -f simple.nix --no-link

# Validate each line is valid JSON
lineNum=0
while IFS= read -r line; do
    lineNum=$((lineNum + 1))
    if ! echo "$line" | jq -e . > /dev/null 2>&1; then
        fail "Invalid JSON at line $lineNum: $line"
    fi
done < "$profilePath"

echo "All JSON lines are valid"

# Test thread safety with parallel operations
rm -f "$profilePath"
export NIX_SQLITE_PROFILE="$profilePath"

# Run multiple nix commands in parallel
(
    nix eval --expr '1 + 1' > /dev/null &
    nix eval --expr '2 + 2' > /dev/null &
    nix eval --expr '3 + 3' > /dev/null &
    wait
)

# Verify no corruption in output
if ! grep -v '^$' "$profilePath" | jq -s . > /dev/null 2>&1; then
    fail "Corrupted JSON output from parallel operations"
fi

echo "Thread safety test passed"

# Cleanup
unset NIX_SQLITE_PROFILE
rm -f "$profilePath"