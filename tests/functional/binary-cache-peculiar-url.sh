#!/usr/bin/env bash

source common.sh

TODO_NixOS

needLocalStore "'--no-require-sigs' can't be used with the daemon"

# Test that narinfo files with absolute URLs for NARs work correctly.
# This tests that the URL field in narinfo can be an absolute URL, not just
# a relative path like "nar/xxx.nar.xz".

clearStore
clearCache
clearCacheCache

outPath=$(nix-build dependencies.nix --no-out-link)
nix copy --to "file://$cacheDir" "$outPath"

# Move NARs to a separate directory outside the cache
narDir="$TEST_ROOT/external-nars"
rm -rf "$narDir"
mkdir -p "$narDir"
mv "$cacheDir"/nar "$narDir"/

# Rewrite narinfo files to use absolute file:// URLs pointing to the external location
# Include a query parameter to test that URL parsing handles it correctly
for narinfo in "$cacheDir"/*.narinfo; do
    sed -i "s|^URL: \(.*\)|URL: file://$narDir/\1?foo=bar|" "$narinfo"
done

# Verify narinfo files now have absolute URLs pointing outside the cache
grep -q "^URL: file://$narDir/" "$cacheDir"/*.narinfo

# Clear the store and fetch using the cache with absolute NAR URLs
clearStore
clearCacheCache

# This should succeed - the store should fetch NARs via absolute file:// URLs
_NIX_FORCE_HTTP=1 nix-store --substituters "file://$cacheDir" --no-require-sigs -r "$outPath"

# Verify the path was successfully fetched
[ -e "$outPath" ]

# Test that local binary cache (without _NIX_FORCE_HTTP) rejects absolute URLs
clearStore
clearCacheCache

# This should fail - local binary cache doesn't support absolute URLs
if nix-store --substituters "file://$cacheDir" --no-require-sigs -r "$outPath" 2>&1; then
    echo "Expected failure: local binary cache should reject absolute URLs in narinfo" >&2
    exit 1
fi

# Test that local binary cache rejects query parameters in relative URLs
clearStore
clearCache
clearCacheCache

nix copy --to "file://$cacheDir" "$outPath"

# Add query parameters to the relative URL
for narinfo in "$cacheDir"/*.narinfo; do
    sed -i 's|^URL: \(.*\)|URL: \1?sha256=test|' "$narinfo"
done

# This should fail - local binary cache doesn't support query parameters
if nix-store --substituters "file://$cacheDir" --no-require-sigs -r "$outPath" 2>&1; then
    echo "Expected failure: local binary cache should reject query parameters" >&2
    exit 1
fi

# But HTTP cache should handle relative URLs with query parameters fine
clearStore
clearCacheCache

_NIX_FORCE_HTTP=1 nix-store --substituters "file://$cacheDir" --no-require-sigs -r "$outPath"
[ -e "$outPath" ]
