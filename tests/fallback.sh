storeExpr=$($TOP/src/nix-instantiate/nix-instantiate fallback.nix)
echo "store expr is $storeExpr"

outPath=$($TOP/src/nix-store/nix-store -q --fallback "$storeExpr")
echo "output path is $outPath"

# Register a non-existant substitute
(echo $outPath && echo $TOP/no-such-program && echo 0 && echo 0) | $TOP/src/nix-store/nix-store --substitute

# Build the derivation
$TOP/src/nix-store/nix-store -r --fallback "$storeExpr"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
