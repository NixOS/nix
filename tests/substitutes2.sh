# Instantiate.
storeExpr=$($TOP/src/nix-instantiate/nix-instantiate substitutes2.nix)
echo "store expr is $storeExpr"

# Find the output path.
outPath=$($TOP/src/nix-store/nix-store -qvvvvv "$storeExpr")
echo "output path is $outPath"

# Build the substitute program.
subProgram=$($TOP/src/nix-store/nix-store -qnf \
    $($TOP/src/nix-instantiate/nix-instantiate substituter.nix))/substituter
echo "substitute program is $subProgram"

# Build the failing substitute program.
subProgram2=$($TOP/src/nix-store/nix-store -qnf \
    $($TOP/src/nix-instantiate/nix-instantiate substituter2.nix))/substituter
echo "failing substitute program is $subProgram2"

regSub() {
    (echo $1 && echo $2 && echo 3 && echo $outPath && echo Hallo && echo Wereld) | $TOP/src/nix-store/nix-store --substitute
}

# Register a fake successor, and a substitute for it.
suc=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab-s.store
regSub $suc $subProgram
$TOP/src/nix-store/nix-store --successor $storeExpr $suc

# Register a failing substitute for it (it takes precedence).
regSub $suc $subProgram2

# Register a substitute for the output path.
regSub $outPath $subProgram

# Register another substitute for the output path.  This one will
# produce other output. 
regSub $outPath $subProgram2


$TOP/src/nix-store/nix-store -rvvvvv "$storeExpr"

text=$(cat "$outPath"/hello)
if test "$text" != "Foo Hallo Wereld"; then exit 1; fi
