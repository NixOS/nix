source common.sh

# Instantiate.
drvPath=$($nixinstantiate substitutes.nix)
echo "derivation is $drvPath"

# Find the output path.
outPath=$($nixstore -qvv "$drvPath")
echo "output path is $outPath"

regSub() {
    (echo $1 && echo "" && echo $2 && echo 3 && echo $outPath && echo Hallo && echo Wereld && echo 0) | $nixstore --register-substitutes
}

# Register a substitute for the output path.
regSub $outPath $(pwd)/substituter.sh


$nixstore -rvv "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hallo Wereld"; then exit 1; fi
