export NIX_BUILD_HOOK="build-hook.hook.sh"

drvPath=$($TOP/src/nix-instantiate/nix-instantiate build-hook.nix)

echo "derivation is $drvPath"

outPath=$($TOP/src/nix-store/nix-store -quf "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "BARBAR"; then exit 1; fi
