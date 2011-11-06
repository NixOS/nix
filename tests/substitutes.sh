source common.sh

clearStore

# Instantiate.
drvPath=$(nix-instantiate simple.nix)
echo "derivation is $drvPath"

# Find the output path.
outPath=$(nix-store -qvv "$drvPath")
echo "output path is $outPath"

echo $outPath > $TEST_ROOT/sub-paths

export NIX_SUBSTITUTERS=$(pwd)/substituter.sh

nix-store -r "$drvPath" --dry-run 2>&1 | grep -q "1.00 MiB.*2.00 MiB"

nix-store -rvv "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hallo Wereld"; then echo "wrong substitute output: $text"; exit 1; fi
