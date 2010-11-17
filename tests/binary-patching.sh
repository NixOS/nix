source common.sh

clearManifests

mkdir -p $TEST_ROOT/cache2 $TEST_ROOT/patches

RESULT=$TEST_ROOT/result

# Build version 1 and 2 of the "foo" package.
$NIX_BIN_DIR/nix-push --copy $TEST_ROOT/cache2 $TEST_ROOT/manifest1 \
    $($nixbuild -o $RESULT binary-patching.nix --arg version 1)

out2=$($nixbuild -o $RESULT binary-patching.nix --arg version 2)
$NIX_BIN_DIR/nix-push --copy $TEST_ROOT/cache2 $TEST_ROOT/manifest2 $out2
rm $RESULT

# Generate a binary patch.
$NIX_BIN_DIR/generate-patches.pl $TEST_ROOT/cache2 $TEST_ROOT/patches \
    file://$TEST_ROOT/patches $TEST_ROOT/manifest1 $TEST_ROOT/manifest2

grep -q "patch {" $TEST_ROOT/manifest2

# Get rid of version 2.
$nixstore --delete $out2
! test -e $out2

# Pull the manifest containing the patch.
clearManifests
$NIX_BIN_DIR/nix-pull file://$TEST_ROOT/manifest2

# To make sure that we're using the patch, delete the full NARs.
rm -f $TEST_ROOT/cache2/*

# Make sure that the download size prediction uses the patch rather
# than the full download.
$nixbuild -o $RESULT binary-patching.nix --arg version 2 --dry-run 2>&1 | grep -q "0.01 MiB"

# Now rebuild it.  This should use the patch generated above.
$nixbuild -o $RESULT binary-patching.nix --arg version 2
