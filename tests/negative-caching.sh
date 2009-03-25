source common.sh

clearStore

set +e

opts="--option build-cache-failure true --print-build-trace"

# This build should fail, and the failure should be cached.
log=$($nixbuild $opts negative-caching.nix -A fail 2>&1) && fail "should fail"
echo "$log" | grep -q "@ build-failed" || fail "no build-failed trace"

# Do it again.  The build shouldn't be tried again.
log=$($nixbuild $opts negative-caching.nix -A fail 2>&1) && fail "should fail"
echo "$log" | grep -q "FAIL" && fail "failed build not cached"
echo "$log" | grep -q "@ build-failed .* cached" || fail "trace doesn't say cached"

# Check that --keep-going works properly with cached failures.
log=$($nixbuild $opts --keep-going negative-caching.nix -A depOnFail 2>&1) && fail "should fail"
echo "$log" | grep -q "FAIL" && fail "failed build not cached (2)"
echo "$log" | grep -q "@ build-failed .* cached" || fail "trace doesn't say cached (2)"
echo "$log" | grep -q "@ build-succeeded .*-succeed" || fail "didn't keep going"
