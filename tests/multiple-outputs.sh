source common.sh

echo "Testing multiple outputs..."

drvPath=$(nix-instantiate multiple-outputs.nix)

echo "derivation is $drvPath"

outPath=$(nix-store -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/file)
if test "$text" != "success"; then exit 1; fi

