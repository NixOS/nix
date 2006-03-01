source common.sh

drvPath=$($nixinstantiate dependencies.nix)

echo "derivation is $drvPath"

$TOP/src/nix-store/nix-store -q --tree "$drvPath" | grep '   +---.*builder1.sh'

# Test Graphviz graph generation.
$nixstore -q --graph "$drvPath" > $TEST_ROOT/graph
if test -n "$dot"; then
    # Does it parse?
    $dot < $TEST_ROOT/graph
fi    

outPath=$($nixstore -rvv "$drvPath")

# Test Graphviz graph generation.
$nixstore -q --graph "$outPath" > $TEST_ROOT/graph
if test -n "$dot"; then
    # Does it parse?
    $dot < $TEST_ROOT/graph
fi    

$nixstore -q --tree "$outPath" | grep '+---.*dependencies-input-2'

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "FOOBAR"; then exit 1; fi

deps=$($nixstore -quR "$drvPath")

echo "output closure contains $deps"

# The output path should be in the closure.
echo "$deps" | grep -q "$outPath"

# Input-1 is not retained.
if echo "$deps" | grep -q "dependencies-input-1"; then exit 1; fi

# Input-2 is retained.
input2OutPath=$(echo "$deps" | grep "dependencies-input-2")

# The referrers closure of input-2 should include outPath.
$nixstore -q --referrers-closure "$input2OutPath" | grep "$outPath"

# Check that the derivers are set properly.
test $($nixstore -q --deriver "$outPath") = "$drvPath"
$nixstore -q --deriver "$input2OutPath" | grep -q -- "-input-2.drv" 
