storeExpr=$($TOP/src/nix-instantiate/nix-instantiate dependencies.nix)

echo "store expr is $storeExpr"

outPath=$($TOP/src/nix-store/nix-store -qnfvvvvv "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "FOOBAR"; then exit 1; fi

deps=$($TOP/src/nix-store/nix-store -qnR "$storeExpr")

echo "output closures are $deps"

# The output path should be in the closure.
echo "$deps" | grep -q "$outPath"

# Input-1 is not retained.
if echo "$deps" | grep -q "dependencies-input-1"; then exit 1; fi

# Input-2 is retained.
echo "$deps" | grep -q "dependencies-input-2"
