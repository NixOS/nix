export NIX_BUILD_HOOK="build-hook.hook.sh"

storeExpr=$($TOP/src/nix-instantiate/nix-instantiate build-hook.nix)

echo "store expr is $storeExpr"

outPath=$($TOP/src/nix-store/nix-store -quf "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "BARBAR"; then exit 1; fi
