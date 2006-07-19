source common.sh

drvPath=$($nixinstantiate simple.nix)

test "$($nixstore -q --binding system "$drvPath")" = "$system"

echo "derivation is $drvPath"

outPath=$($nixstore -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi

# Directed delete: $outPath is not reachable from a root, so it should
# be deleteable.
$nixstore --delete $outPath
if test -e $outPath/hello; then false; fi

if test "$(NIX_STORE_DIR=/foo $nixinstantiate --readonly-mode hash-check.nix)" != "/foo/4hgkkq63lp8x5kmh9cmsyqimq5v42zzl-dependencies.drv"; then
    echo "hashDerivationModulo appears broken"
fi
