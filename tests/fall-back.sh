storeExpr=$($TOP/src/nix-instantiate/nix-instantiate fall-back.nix)

echo "store expr is $storeExpr"

# Register a non-existant successor.
suc=$NIX_STORE_DIR/deadbeafdeadbeafdeadbeafdeadbeaf-s.store
$TOP/src/nix-store/nix-store --successor $storeExpr $suc

outPath=$($TOP/src/nix-store/nix-store -qnfvvvvv "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
