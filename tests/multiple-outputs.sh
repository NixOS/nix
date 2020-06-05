source common.sh

clearStore

rm -f $TEST_ROOT/result*

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
outPath=$(nix-build multiple-outputs.nix -A d --no-out-link)
drvPath=$(cat $outPath/drv)
outPath=$(nix-store -q $drvPath)
(! [ -e "$outPath" ])

# Do a build of something that depends on a derivation with multiple
# outputs.
echo "building b..."
outPath=$(nix-build multiple-outputs.nix -A b --no-out-link)
echo "output path is $outPath"
[ "$(cat "$outPath"/file)" = "success" ]

# Test nix-build on a derivation with multiple outputs.
outPath1=$(nix-build multiple-outputs.nix -A a -o $TEST_ROOT/result)
[ -e $TEST_ROOT/result-first ]
(! [ -e $TEST_ROOT/result-second ])
nix-build multiple-outputs.nix -A a.all -o $TEST_ROOT/result
[ "$(cat $TEST_ROOT/result-first/file)" = "first" ]
[ "$(cat $TEST_ROOT/result-second/file)" = "second" ]
[ "$(cat $TEST_ROOT/result-second/link/file)" = "first" ]
hash1=$(nix-store -q --hash $TEST_ROOT/result-second)

outPath2=$(nix-build $(nix-instantiate multiple-outputs.nix -A a) --no-out-link)
[[ $outPath1 = $outPath2 ]]

outPath2=$(nix-build $(nix-instantiate multiple-outputs.nix -A a.first) --no-out-link)
[[ $outPath1 = $outPath2 ]]

outPath2=$(nix-build $(nix-instantiate multiple-outputs.nix -A a.second) --no-out-link)
[[ $(cat $outPath2/file) = second ]]

[[ $(nix-build $(nix-instantiate multiple-outputs.nix -A a.all) --no-out-link | wc -l) -eq 2 ]]

# Delete one of the outputs and rebuild it.  This will cause a hash
# rewrite.
nix-store --delete $TEST_ROOT/result-second --ignore-liveness
nix-build multiple-outputs.nix -A a.all -o $TEST_ROOT/result
[ "$(cat $TEST_ROOT/result-second/file)" = "second" ]
[ "$(cat $TEST_ROOT/result-second/link/file)" = "first" ]
hash2=$(nix-store -q --hash $TEST_ROOT/result-second)
[ "$hash1" = "$hash2" ]

# Make sure that nix-build works on derivations with multiple outputs.
echo "building a.first..."
nix-build multiple-outputs.nix -A a.first --no-out-link

# Cyclic outputs should be rejected.
echo "building cyclic..."
if nix-build multiple-outputs.nix -A cyclic --no-out-link; then
    echo "Cyclic outputs incorrectly accepted!"
    exit 1
fi

echo "collecting garbage..."
rm $TEST_ROOT/result*
nix-store --gc --keep-derivations --keep-outputs
nix-store --gc --print-roots
