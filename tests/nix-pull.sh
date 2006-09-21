source common.sh

pullCache () {
    echo "pulling cache..."
    $PERL -w -I$TOP/scripts $TOP/scripts/nix-pull file://$TEST_ROOT/manifest
}

clearStore
pullCache

drvPath=$($nixinstantiate dependencies.nix)
outPath=$($nixstore -q $drvPath)

echo "building $outPath using substitutes..."
$nixstore -r $outPath

cat $outPath/input-2/bar

clearStore
pullCache

echo "building $drvPath using substitutes..."
$nixstore -r $drvPath

cat $outPath/input-2/bar

# Check that the derivers are set properly.
test $($nixstore -q --deriver "$outPath") = "$drvPath"
$nixstore -q --deriver $(readLink $outPath/input-2) | grep -q -- "-input-2.drv" 

$nixstore --clear-substitutes
