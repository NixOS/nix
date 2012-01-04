source common.sh

clearStore

# Test whether read-only evaluation works when referring to the
# ‘drvPath’ attribute.
echo "evaluating c..."
#drvPath=$(nix-instantiate multiple-outputs.nix -A c --readonly-mode)

# And check whether the resulting derivation explicitly depends on all
# outputs.
drvPath=$(nix-instantiate multiple-outputs.nix -A c)
#[ "$drvPath" = "$drvPath2" ]
grep -q 'multiple-outputs-a.drv",\["first","second"\]' $drvPath
grep -q 'multiple-outputs-b.drv",\["out"\]' $drvPath

# While we're at it, test the ‘unsafeDiscardOutputDependency’ primop.
outPath=$(nix-build multiple-outputs.nix -A d)
drvPath=$(cat $outPath/drv)
outPath=$(nix-store -q $drvPath)
! [ -e "$outPath" ]

# Do a build of something that depends on a derivation with multiple
# outputs.
echo "building b..."
outPath=$(nix-build multiple-outputs.nix -A b)
echo "output path is $outPath"
[ "$(cat "$outPath"/file)" = "success" ]

# Make sure that nix-build works on derivations with multiple outputs.
echo "building a.first..."
nix-build multiple-outputs.nix -A a.first

# Cyclic outputs should be rejected.
echo "building cyclic..."
if nix-build multiple-outputs.nix -A cyclic; then
    echo "Cyclic outputs incorrectly accepted!"
    exit 1
fi

echo "collecting garbage..."
nix-store --gc
