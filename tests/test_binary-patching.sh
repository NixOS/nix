source common.sh

clearManifests

mkdir -p $TEST_ROOT/cache2 $TEST_ROOT/patches

RESULT=$TEST_ROOT/result

# Build version 1 and 2 of the "foo" package.
nix-push --dest $TEST_ROOT/cache2 --manifest --bzip2 \
    $(nix-build -o $RESULT binary-patching.nix --arg version 1)
mv $TEST_ROOT/cache2/MANIFEST $TEST_ROOT/manifest1 

out2=$(nix-build -o $RESULT binary-patching.nix --arg version 2)
nix-push --dest $TEST_ROOT/cache2 --manifest --bzip2 $out2
mv $TEST_ROOT/cache2/MANIFEST $TEST_ROOT/manifest2
    
out3=$(nix-build -o $RESULT binary-patching.nix --arg version 3)
nix-push --dest $TEST_ROOT/cache2 --manifest --bzip2 $out3
mv $TEST_ROOT/cache2/MANIFEST $TEST_ROOT/manifest3

rm $RESULT

# Generate binary patches.
nix-generate-patches $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest1 $TEST_ROOT/manifest2

nix-generate-patches $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest2 $TEST_ROOT/manifest3

grep -q "patch {" $TEST_ROOT/manifest3

# Get rid of versions 2 and 3.
nix-store --delete $out2 $out3

# Pull the manifest containing the patches.
clearManifests
nix-pull file://$TEST_ROOT/manifest3

# Make sure that the download size prediction uses the patches rather
# than the full download.
nix-build -o $RESULT binary-patching.nix --arg version 3 --dry-run 2>&1 | grep -q "0.01 MiB"

# Now rebuild it.  This should use the two patches generated above.
rm -f $TEST_ROOT/var/log/nix/downloads
nix-build -o $RESULT binary-patching.nix --arg version 3
rm $RESULT
[ "$(grep ' patch ' $TEST_ROOT/var/log/nix/downloads | wc -l)" -eq 2 ]

# Add a patch from version 1 directly to version 3.
nix-generate-patches $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest1 $TEST_ROOT/manifest3

# Rebuild version 3.  This should use the direct patch rather than the
# sequence of two patches.
nix-store --delete $out2 $out3
clearManifests
rm $TEST_ROOT/var/log/nix/downloads
nix-pull file://$TEST_ROOT/manifest3
nix-build -o $RESULT binary-patching.nix --arg version 3
[ "$(grep ' patch ' $TEST_ROOT/var/log/nix/downloads | wc -l)" -eq 1 ]
