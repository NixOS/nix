source common.sh

echo "Testing multiple outputs..."

outPath=$(nix-build multiple-outputs.nix -A b)
echo "output path is $outPath"
[ "$(cat "$outPath"/file)" = "success" ]

# Make sure that nix-build works on derivations with multiple outputs.
nix-build multiple-outputs.nix -A a.first

# Cyclic outputs should be rejected.
if nix-build multiple-outputs.nix -A cyclic; then
    echo "Cyclic outputs incorrectly accepted!"
    exit 1
fi

clearStore
