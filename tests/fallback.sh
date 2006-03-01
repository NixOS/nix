source common.sh

drvPath=$($nixinstantiate fallback.nix)
echo "derivation is $drvPath"

outPath=$($nixstore -q --fallback "$drvPath")
echo "output path is $outPath"

# Register a non-existant substitute
(echo $outPath && echo "" && echo $TOP/no-such-program && echo 0 && echo 0) | $nixstore --register-substitutes

# Build the derivation
$nixstore -r --fallback "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
