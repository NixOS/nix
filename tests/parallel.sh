storeExpr=$($TOP/src/nix-instantiate/nix-instantiate parallel.nix)

echo "store expr is $storeExpr"

outPath=$($TOP/src/nix-store/nix-store -qnfvvvv -j0 "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "abacade"; then exit 1; fi

if test "$(cat $SHARED.cur)" != 0; then exit 1; fi
if test "$(cat $SHARED.max)" != 3; then exit 1; fi
