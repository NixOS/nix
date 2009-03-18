source common.sh

export NIX_BUILD_HOOK="build-hook.hook.sh"

outPath=$($nixbuild build-hook.nix)

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "BARBAR"; then exit 1; fi
