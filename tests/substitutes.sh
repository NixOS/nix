source common.sh

clearStore

# Instantiate.
drvPath=$($nixinstantiate simple.nix)
echo "derivation is $drvPath"

# Find the output path.
outPath=$($nixstore -qvv "$drvPath")
echo "output path is $outPath"

echo $outPath > $TEST_ROOT/sub-paths

export NIX_SUBSTITUTERS=$(pwd)/substituter.sh

$nixstore -rvv "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hallo Wereld"; then echo "wrong substitute output: $text"; exit 1; fi
