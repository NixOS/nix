storeExpr=$($TOP/src/nix-instantiate/nix-instantiate simple.nix)

echo "store expr is $storeExpr"

outPath=$($TOP/src/nix-store/nix-store -bvv "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
