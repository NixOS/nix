# Instantiate.
storeExpr=$($TOP/src/nix-instantiate/nix-instantiate substitutes.nix)
echo "store expr is $storeExpr"

# Find the output path.
outPath=$($TOP/src/nix-store/nix-store -qvvvvv "$storeExpr")
echo "output path is $outPath"

# Instantiate the substitute program.
subExpr=$($TOP/src/nix-instantiate/nix-instantiate substituter.nix)
echo "store expr is $subExpr"

# Register a fake successor, and a substitute for it.
suc=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-s.store
(echo $suc && echo $subExpr && echo "/substituter" && echo 3 && echo $outPath && echo Hallo && echo Wereld) | $TOP/src/nix-store/nix-store --substitute
$TOP/src/nix-store/nix-store --successor $storeExpr $suc

# Register a substitute for the output path.
(echo $outPath && echo $subExpr && echo "/substituter" && echo 3 && echo $outPath && echo Hallo && echo Wereld) | $TOP/src/nix-store/nix-store --substitute


$TOP/src/nix-store/nix-store -rvvvvv "$storeExpr"

text=$(cat "$outPath"/hello)
if test "$text" != "Hallo Wereld"; then exit 1; fi
