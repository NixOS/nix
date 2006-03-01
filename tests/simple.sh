source common.sh

drvPath=$($nixinstantiate simple.nix)

echo "derivation is $drvPath"

outPath=$($nixstore -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
