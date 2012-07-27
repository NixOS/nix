source common.sh

clearStore

# Create the binary cache.
cacheDir=$TEST_ROOT/binary-cache
rm -rf $cacheDir

outPath=$(nix-build dependencies.nix --no-out-link)

nix-push --dest $cacheDir $outPath

# Check that downloading works.
clearStore
rm -f $NIX_STATE_DIR/binary-cache*

NIX_BINARY_CACHES="file://$cacheDir" nix-env -f dependencies.nix -qas \* | grep -- "--S"

NIX_BINARY_CACHES="file://$cacheDir" nix-store -r $outPath

nix-store --check-validity $outPath
nix-store -qR $outPath | grep input-2
