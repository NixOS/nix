source common.sh

drvPath=$($nixinstantiate dependencies.nix)
outPath=$($nixstore -rvv "$drvPath")

# Set a GC root.
rm -f "$NIX_STATE_DIR"/gcroots/foo
ln -sf $outPath "$NIX_STATE_DIR"/gcroots/foo

$nixstore --gc --print-roots | grep $outPath
$nixstore --gc --print-live | grep $outPath
$nixstore --gc --print-dead | grep $drvPath
if $nixstore --gc --print-dead | grep $outPath; then false; fi

$nixstore --gc --print-dead

inUse=$(readLink $outPath/input-2)
if $nixstore --delete $inUse; then false; fi
test -e $inUse

if $nixstore --delete $outPath; then false; fi
test -e $outPath

$NIX_BIN_DIR/nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat $outPath/foobar
cat $outPath/input-2/bar

# Check that the derivation has been GC'd.
if test -e $drvPath; then false; fi

rm "$NIX_STATE_DIR"/gcroots/foo

$NIX_BIN_DIR/nix-collect-garbage

# Check that the output has been GC'd.
if test -e $outPath/foobar; then false; fi
