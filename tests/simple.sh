drvPath=$($TOP/src/nix-instantiate/nix-instantiate simple.nix)

echo "derivation is $drvPath"

outPath=$($TOP/src/nix-store/nix-store -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
