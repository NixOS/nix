source common.sh

clearStore

drvPath=$($nixinstantiate simple.nix)
echo "derivation is $drvPath"

outPath=$($nixstore -q --fallback "$drvPath")
echo "output path is $outPath"

# Build with a substitute that fails.  This should fail.
export NIX_SUBSTITUTERS=$(pwd)/substituter2.sh
if $nixstore -r "$drvPath"; then echo unexpected fallback; exit 1; fi

# Build with a substitute that fails.  This should fall back to a source build.
export NIX_SUBSTITUTERS=$(pwd)/substituter2.sh
$nixstore -r --fallback "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
