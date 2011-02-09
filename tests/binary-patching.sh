source common.sh

clearManifests

mkdir -p $TEST_ROOT/cache2 $TEST_ROOT/patches

RESULT=$TEST_ROOT/result

# Build version 1 and 2 of the "foo" package.
$NIX_BIN_DIR/nix-push --copy $TEST_ROOT/cache2 $TEST_ROOT/manifest1 \
    $($nixbuild -o $RESULT binary-patching.nix --arg version 1)

out2=$($nixbuild -o $RESULT binary-patching.nix --arg version 2)
$NIX_BIN_DIR/nix-push --copy $TEST_ROOT/cache2 $TEST_ROOT/manifest2 $out2
    
out3=$($nixbuild -o $RESULT binary-patching.nix --arg version 3)
$NIX_BIN_DIR/nix-push --copy $TEST_ROOT/cache2 $TEST_ROOT/manifest3 $out3

rm $RESULT

# Generate binary patches.
$NIX_BIN_DIR/nix-generate-patches $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest1 $TEST_ROOT/manifest2

$NIX_BIN_DIR/nix-generate-patches $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest2 $TEST_ROOT/manifest3

grep -q "patch {" $TEST_ROOT/manifest3

# Get rid of versions 2 and 3.
$nixstore --delete $out2 $out3

# Pull the manifest containing the patches.
clearManifests
$NIX_BIN_DIR/nix-pull file://$TEST_ROOT/manifest3

# Make sure that the download size prediction uses the patches rather
# than the full download.
$nixbuild -o $RESULT binary-patching.nix --arg version 3 --dry-run 2>&1 | grep -q "0.01 MiB"

# Now rebuild it.  This should use the two patches generated above.
rm -f $TEST_ROOT/var/log/nix/downloads
$nixbuild -o $RESULT binary-patching.nix --arg version 3
rm $RESULT
[ "$(grep ' patch ' $TEST_ROOT/var/log/nix/downloads | wc -l)" -eq 2 ]

# Add a patch from version 1 directly to version 3.
$NIX_BIN_DIR/nix-generate-patches $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest1 $TEST_ROOT/manifest3

# Rebuild version 3.  This should use the direct patch rather than the
# sequence of two patches.
$nixstore --delete $out2 $out3
clearManifests
rm $TEST_ROOT/var/log/nix/downloads
$NIX_BIN_DIR/nix-pull file://$TEST_ROOT/manifest3
$nixbuild -o $RESULT binary-patching.nix --arg version 3
[ "$(grep ' patch ' $TEST_ROOT/var/log/nix/downloads | wc -l)" -eq 1 ]
