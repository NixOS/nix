source common.sh

clearStore
clearManifests

# Create the binary cache.
cacheDir=$TEST_ROOT/binary-cache
rm -rf $cacheDir

outPath=$(nix-build dependencies.nix --no-out-link)

nix-push --dest $cacheDir $outPath


# By default, a binary cache doesn't support "nix-env -qas", but does
# support installation.
clearStore
rm -f $NIX_STATE_DIR/binary-cache*

nix-env --option binary-caches "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "---"

nix-store --option binary-caches "file://$cacheDir" -r $outPath


# But with the right configuration, "nix-env -qas" should also work.
clearStore
rm -f $NIX_STATE_DIR/binary-cache*
echo "WantMassQuery: 1" >> $cacheDir/nix-cache-info

nix-env --option binary-caches "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "--S"

x=$(nix-env -f dependencies.nix -qas \* --prebuilt-only)
[ -z "$x" ]

nix-store --option binary-caches "file://$cacheDir" -r $outPath

nix-store --check-validity $outPath
nix-store -qR $outPath | grep input-2


# Test whether building works if the binary cache contains an
# incomplete closure.
clearStore

rm $(grep -l "StorePath:.*dependencies-input-2" $cacheDir/*.narinfo)

nix-build --option binary-caches "file://$cacheDir" dependencies.nix -o $TEST_ROOT/result
