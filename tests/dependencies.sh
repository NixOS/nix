drvPath=$($TOP/src/nix-instantiate/nix-instantiate dependencies.nix)

echo "derivation is $drvPath"

outPath=$($TOP/src/nix-store/nix-store -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "FOOBAR"; then exit 1; fi

deps=$($TOP/src/nix-store/nix-store -quR "$drvPath")

echo "output closure contains $deps"

# The output path should be in the closure.
echo "$deps" | grep -q "$outPath"

# Input-1 is not retained.
if echo "$deps" | grep -q "dependencies-input-1"; then exit 1; fi

# Input-2 is retained.
input2OutPath=$(echo "$deps" | grep "dependencies-input-2")

# The referers closure of input-2 should include outPath.
$TOP/src/nix-store/nix-store -q --referers-closure "$input2OutPath" | grep "$outPath"

# Check that the derivers are set properly.
test $($TOP/src/nix-store/nix-store -q --deriver "$outPath") = "$drvPath"
$TOP/src/nix-store/nix-store -q --deriver "$input2OutPath" | grep -q -- "-input-2.drv" 
