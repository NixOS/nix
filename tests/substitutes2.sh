# Instantiate.
storeExpr=$($TOP/src/nix-instantiate/nix-instantiate substitutes2.nix)
echo "store expr is $storeExpr"

# Find the output path.
outPath=$($TOP/src/nix-store/nix-store -qvvvvv "$storeExpr")
echo "output path is $outPath"

# Instantiate the substitute program.
subExpr=$($TOP/src/nix-instantiate/nix-instantiate substituter.nix)
echo "store expr is $subExpr"

# Instantiate the failing substitute program.
subExpr2=$($TOP/src/nix-instantiate/nix-instantiate substituter2.nix)
echo "store expr is $subExpr2"

regSub() {
    (echo $1 && echo $2 && echo "/substituter" && echo 3 && echo $outPath && echo Hallo && echo Wereld) | $TOP/src/nix-store/nix-store --substitute
}

# Register a fake successor, and a substitute for it.
suc=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab-s.store
regSub $suc $subExpr
$TOP/src/nix-store/nix-store --successor $storeExpr $suc

# Register a failing substitute for it (it takes precedence).
regSub $suc $subExpr2

# Register a substitute for the output path.
regSub $outPath $subExpr

# Register another substitute for the output path.  This one will
# produce other output. 
regSub $outPath $subExpr2


$TOP/src/nix-store/nix-store -rvvvvv "$storeExpr"

text=$(cat "$outPath"/hello)
if test "$text" != "Foo Hallo Wereld"; then exit 1; fi
