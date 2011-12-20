source common.sh

echo "Testing multiple outputs..."

outPath=$(nix-build multiple-outputs.nix -A b)
echo "output path is $outPath"
[ "$(cat "$outPath"/file)" = "success" ]
