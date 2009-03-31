source common.sh

clearStore

# Instantiate.
drvPath=$($nixinstantiate simple.nix)
echo "derivation is $drvPath"

# Find the output path.
outPath=$($nixstore -qvvvvv "$drvPath")
echo "output path is $outPath"

echo $outPath > $TEST_ROOT/sub-paths

# First try a substituter that fails, then one that succeeds
export NIX_SUBSTITUTERS=$(pwd)/substituter2.sh:$(pwd)/substituter.sh

$nixstore -j0 -rvv "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hallo Wereld"; then echo "wrong substitute output: $text"; exit 1; fi
