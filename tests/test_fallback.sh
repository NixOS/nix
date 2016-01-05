source common.sh

clearStore

drvPath=$(nix-instantiate simple.nix)
echo "derivation is $drvPath"

outPath=$(nix-store -q --fallback "$drvPath")
echo "output path is $outPath"

# Build with a substitute that fails.  This should fail.
export NIX_SUBSTITUTERS=$(pwd)/substituter2.sh
if nix-store -r "$drvPath"; then echo unexpected fallback; exit 1; fi

# Build with a substitute that fails.  This should fall back to a source build.
export NIX_SUBSTITUTERS=$(pwd)/substituter2.sh
nix-store -r --fallback "$drvPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi
