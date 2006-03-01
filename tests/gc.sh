source common.sh

drvPath=$($nixinstantiate dependencies.nix)
outPath=$($nixstore -rvv "$drvPath")

# Set a GC root.
ln -s $outPath "$NIX_STATE_DIR"/gcroots/foo

$NIX_BIN_DIR/nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat $outPath/foobar
cat $outPath/input-2/bar

# Check that the derivation has been GC'd.
if cat $drvPath > /dev/null; then false; fi
