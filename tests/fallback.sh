storeExpr=$($TOP/src/nix-instantiate/nix-instantiate fallback.nix)

echo "store expr is $storeExpr"

# Register a non-existant successor (and a nox-existant substitute).
suc=$NIX_STORE_DIR/deadbeafdeadbeafdeadbeafdeadbeaf-s.store
(echo $suc && echo $NIX_STORE_DIR/ffffffffffffffffffffffffffffffff.store && echo "/bla" && echo 0) | $TOP/src/nix-store/nix-store --substitute
$TOP/src/nix-store/nix-store --successor $storeExpr $suc

outPath=$($TOP/src/nix-store/nix-store -qnf --fallback "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
