source common.sh

pullCache () {
    echo "pulling cache..."
    nix-pull file://$TEST_ROOT/cache/MANIFEST
}

clearStore
clearManifests
pullCache

drvPath=$(nix-instantiate dependencies.nix)
outPath=$(nix-store -q $drvPath)

echo "building $outPath using substitutes..."
nix-store -r $outPath

cat $outPath/input-2/bar

clearStore
clearManifests
pullCache

echo "building $drvPath using substitutes..."
nix-store -r $drvPath

cat $outPath/input-2/bar

# Check that the derivers are set properly.
test $(nix-store -q --deriver "$outPath") = "$drvPath"
nix-store -q --deriver $(readLink $outPath/input-2) | grep -q -- "-input-2.drv"

clearManifests
